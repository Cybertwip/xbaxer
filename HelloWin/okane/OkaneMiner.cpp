#include "pch.h"
#include "OkaneMiner.h"
#include "OkaneGpuHasher.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#pragma comment(lib, "ws2_32.lib")

// ============================================================================
// Helpers
// ============================================================================

namespace
{

std::string TrimWhitespace(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool DecodeHex(const std::string& hex, uint8_t* out, size_t expectedLen)
{
    if (hex.size() != expectedLen * 2) return false;
    for (size_t i = 0; i < expectedLen; ++i)
    {
        unsigned int byte = 0;
        if (sscanf_s(hex.c_str() + i * 2, "%02x", &byte) != 1) return false;
        out[i] = static_cast<uint8_t>(byte);
    }
    return true;
}

std::string EncodeHex(const uint8_t* data, size_t len)
{
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        char buf[3];
        sprintf_s(buf, "%02x", data[i]);
        result.append(buf);
    }
    return result;
}

// Minimal JSON value extraction — good enough for Bitcoin Core RPC responses.
// Does not handle nested objects or escaped quotes in values.
std::string JsonGetString(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    ++pos;
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return {};
    return json.substr(pos, end - pos);
}

int64_t JsonGetInt(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return 0;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    return std::strtoll(json.c_str() + pos, nullptr, 10);
}

uint64_t JsonGetUInt64(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return 0;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    return std::strtoull(json.c_str() + pos, nullptr, 10);
}

// Find the "result" value in an RPC response.  Returns everything between
// "result": and the next top-level comma or closing brace.
std::string JsonGetResult(const std::string& json)
{
    size_t pos = json.find("\"result\"");
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + 8);
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < json.size() && json[pos] == ' ') ++pos;

    if (pos >= json.size()) return {};

    // If result is a string
    if (json[pos] == '"')
    {
        ++pos;
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return {};
        return json.substr(pos, end - pos);
    }

    // If result is an object or null
    if (json[pos] == '{')
    {
        int depth = 0;
        size_t start = pos;
        for (; pos < json.size(); ++pos)
        {
            if (json[pos] == '{') ++depth;
            else if (json[pos] == '}') { --depth; if (depth == 0) return json.substr(start, pos - start + 1); }
        }
        return {};
    }

    // null or number
    size_t start = pos;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}') ++pos;
    std::string val = json.substr(start, pos - start);
    return TrimWhitespace(val);
}

// Check for "error" field in RPC response.
std::string JsonGetError(const std::string& json)
{
    size_t pos = json.find("\"error\"");
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + 7);
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos < json.size() && json[pos] == 'n') return {}; // null

    // It's an object — extract "message"
    size_t objStart = pos;
    if (json[pos] == '{')
    {
        int depth = 0;
        size_t end = pos;
        for (; end < json.size(); ++end)
        {
            if (json[end] == '{') ++depth;
            else if (json[end] == '}') { --depth; if (depth == 0) break; }
        }
        std::string errObj = json.substr(objStart, end - objStart + 1);
        std::string msg = JsonGetString(errObj, "message");
        if (!msg.empty()) return msg;
        return "RPC error";
    }
    return {};
}

void AppendLE32(std::vector<uint8_t>& buf, uint32_t val)
{
    buf.push_back(static_cast<uint8_t>(val));
    buf.push_back(static_cast<uint8_t>(val >> 8));
    buf.push_back(static_cast<uint8_t>(val >> 16));
    buf.push_back(static_cast<uint8_t>(val >> 24));
}

void AppendLE64(std::vector<uint8_t>& buf, uint64_t val)
{
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<uint8_t>(val >> (i * 8)));
}

void WriteLE32(uint8_t* dst, uint32_t val)
{
    dst[0] = static_cast<uint8_t>(val);
    dst[1] = static_cast<uint8_t>(val >> 8);
    dst[2] = static_cast<uint8_t>(val >> 16);
    dst[3] = static_cast<uint8_t>(val >> 24);
}

uint32_t ReadLE32(const uint8_t* src)
{
    return static_cast<uint32_t>(src[0])
         | (static_cast<uint32_t>(src[1]) << 8)
         | (static_cast<uint32_t>(src[2]) << 16)
         | (static_cast<uint32_t>(src[3]) << 24);
}

} // anonymous namespace

// ============================================================================
// SHA-256 — fully inlined, no kernel calls
// ============================================================================

namespace
{

static constexpr uint32_t SHA256_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t Ch(uint32_t e, uint32_t f, uint32_t g) { return (e & f) ^ (~e & g); }
inline uint32_t Maj(uint32_t a, uint32_t b, uint32_t c) { return (a & b) ^ (a & c) ^ (b & c); }
inline uint32_t Sigma0(uint32_t a) { return rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22); }
inline uint32_t Sigma1(uint32_t e) { return rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25); }
inline uint32_t sigma0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
inline uint32_t sigma1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

inline uint32_t ReadBE32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline void WriteBE32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

struct Sha256State
{
    uint32_t h[8];
};

static constexpr Sha256State SHA256_INIT = {{
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ce935,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
}};

// Compress one 64-byte block into state.
inline void Sha256Transform(Sha256State& state, const uint8_t* block)
{
    uint32_t W[64];
    for (int i = 0; i < 16; ++i)
        W[i] = ReadBE32(block + i * 4);
    for (int i = 16; i < 64; ++i)
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];

    uint32_t a = state.h[0], b = state.h[1], c = state.h[2], d = state.h[3];
    uint32_t e = state.h[4], f = state.h[5], g = state.h[6], h = state.h[7];

    for (int i = 0; i < 64; ++i)
    {
        uint32_t t1 = h + Sigma1(e) + Ch(e, f, g) + SHA256_K[i] + W[i];
        uint32_t t2 = Sigma0(a) + Maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state.h[0] += a; state.h[1] += b; state.h[2] += c; state.h[3] += d;
    state.h[4] += e; state.h[5] += f; state.h[6] += g; state.h[7] += h;
}

// Compress with pre-expanded W (for the second block of the header where
// only the nonce changes — we pre-expand W[0..2] and only update W[3]).
inline void Sha256TransformW(Sha256State& state, uint32_t W[64])
{
    uint32_t a = state.h[0], b = state.h[1], c = state.h[2], d = state.h[3];
    uint32_t e = state.h[4], f = state.h[5], g = state.h[6], h = state.h[7];

    for (int i = 0; i < 64; ++i)
    {
        uint32_t t1 = h + Sigma1(e) + Ch(e, f, g) + SHA256_K[i] + W[i];
        uint32_t t2 = Sigma0(a) + Maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state.h[0] += a; state.h[1] += b; state.h[2] += c; state.h[3] += d;
    state.h[4] += e; state.h[5] += f; state.h[6] += g; state.h[7] += h;
}

// Full SHA-256 (arbitrary length).
Hash32 Sha256Full(const uint8_t* data, size_t len)
{
    Sha256State state = SHA256_INIT;

    // Process complete 64-byte blocks.
    size_t blocks = len / 64;
    for (size_t i = 0; i < blocks; ++i)
        Sha256Transform(state, data + i * 64);

    // Pad last block(s).
    size_t remaining = len % 64;
    uint8_t pad[128]{};
    std::memcpy(pad, data + blocks * 64, remaining);
    pad[remaining] = 0x80;

    if (remaining >= 56)
    {
        // Need two padding blocks.
        uint64_t bitLen = static_cast<uint64_t>(len) * 8;
        WriteBE32(pad + 120, static_cast<uint32_t>(bitLen));
        WriteBE32(pad + 124, static_cast<uint32_t>(bitLen >> 32));
        // Fix: big-endian 64-bit length
        WriteBE32(pad + 120, static_cast<uint32_t>(bitLen >> 32));
        WriteBE32(pad + 124, static_cast<uint32_t>(bitLen));
        Sha256Transform(state, pad);
        Sha256Transform(state, pad + 64);
    }
    else
    {
        uint64_t bitLen = static_cast<uint64_t>(len) * 8;
        WriteBE32(pad + 56, static_cast<uint32_t>(bitLen >> 32));
        WriteBE32(pad + 60, static_cast<uint32_t>(bitLen));
        Sha256Transform(state, pad);
    }

    Hash32 result{};
    for (int i = 0; i < 8; ++i)
        WriteBE32(result.data() + i * 4, state.h[i]);
    return result;
}

// Precompute the SHA-256 midstate for the first 64 bytes of an 80-byte header.
// This is the key optimization: the first 64 bytes don't change as nonce varies.
inline Sha256State Sha256Midstate(const uint8_t* header64)
{
    Sha256State state = SHA256_INIT;
    Sha256Transform(state, header64);
    return state;
}

// Prepare the second block (bytes 64-79 of header + padding) with W pre-expansion.
// Layout: header[64..79] (16 bytes) + 0x80 + zeros + bit-length(640 = 80*8)
// W[0] = header[64..67] (ntime)
// W[1] = header[68..71] (nbits)  — wait, header layout:
//   offset 68 = ntime, offset 72 = nbits, offset 76 = nonce
// Actually for Bitcoin mining header:
//   [0..3] version, [4..35] prevhash, [36..67] merkle, [68..71] ntime, [72..75] nbits, [76..79] nonce
// So second block bytes 64-79:
//   W[0] = bytes[64..67] = merkle[28..31]  (last 4 bytes of merkle)
//   W[1] = bytes[68..71] = ntime
//   W[2] = bytes[72..75] = nbits
//   W[3] = bytes[76..79] = nonce  <-- only this changes
//   W[4] = 0x80000000 (padding)
//   W[5..14] = 0
//   W[15] = 640 (bit length = 80 * 8)
inline void PrepareSecondBlockW(uint32_t W[64], const uint8_t* header)
{
    W[0] = ReadBE32(header + 64);
    W[1] = ReadBE32(header + 68);
    W[2] = ReadBE32(header + 72);
    W[3] = 0; // nonce placeholder
    W[4] = 0x80000000;
    for (int i = 5; i < 15; ++i) W[i] = 0;
    W[15] = 640; // 80 bytes * 8 bits

    // Pre-expand W[16..63] that don't depend on W[3] (nonce).
    // W[16] depends on W[0], W[1], W[14]=0, W[15]=640 => no W[3] dep
    // W[17] depends on W[1], W[2], W[15], W[0] => no W[3] dep
    // W[18] depends on W[2], W[3] => DEPENDS on nonce!
    // So only W[16] and W[17] can be pre-expanded.
    W[16] = sigma1(W[14]) + W[9] + sigma0(W[1]) + W[0];
    W[17] = sigma1(W[15]) + W[10] + sigma0(W[2]) + W[1];
    // W[18..63] will be computed in the hot loop after setting W[3].
}

// Fast: complete W expansion from W[18] onwards (W[3] = nonce just set).
inline void ExpandW_FromNonce(uint32_t W[64])
{
    W[18] = sigma1(W[16]) + W[11] + sigma0(W[3]) + W[2];
    W[19] = sigma1(W[17]) + W[12] + sigma0(W[4]) + W[3];
    for (int i = 20; i < 64; ++i)
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];
}

// Second hash: SHA-256 of 32-byte first hash.  Input is 32 bytes.
// Single block: 32 bytes + 0x80 + padding + length(256).
inline void PrepareHashBlockW(uint32_t W[64], const Sha256State& firstHash)
{
    W[0] = firstHash.h[0]; W[1] = firstHash.h[1];
    W[2] = firstHash.h[2]; W[3] = firstHash.h[3];
    W[4] = firstHash.h[4]; W[5] = firstHash.h[5];
    W[6] = firstHash.h[6]; W[7] = firstHash.h[7];
    W[8] = 0x80000000;
    for (int i = 9; i < 15; ++i) W[i] = 0;
    W[15] = 256;
    for (int i = 16; i < 64; ++i)
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];
}

// Quick check: for Bitcoin, a valid hash has the top 4 bytes as zero in big-endian.
// Our hash state h[7] corresponds to the last 4 bytes of the BE hash, which maps to
// the FIRST 4 bytes of the hash output.  Wait — SHA-256 output byte order:
// output[0..3] = h[0] big-endian.  For Bitcoin, the hash must be < target, meaning
// the leading bytes must be zero.  So h[0] == 0 is a necessary (but not sufficient)
// check for a valid share at reasonable difficulty.
// Actually: the final hash is written big-endian: byte[0..3] = h[0].
// For mining, we need hash (as 256-bit LE number in our Hash32) to be < target.
// Hash32[31..28] = h[0] bytes = most significant bytes in our LE storage.
// So if h[0] != 0, the hash is definitely too large for any reasonable difficulty.
// This lets us skip the full target comparison ~99.99% of the time.
inline bool QuickRejectHash(const Sha256State& state)
{
    return state.h[0] != 0;
}

} // namespace

Hash32 DoubleSHA256(const uint8_t* data, size_t len)
{
    Hash32 first = Sha256Full(data, len);
    Hash32 second = Sha256Full(first.data(), 32);
    return second;
}

bool HashLessThan(const Hash32& hash, const Hash32& target)
{
    // Compare from the end (most significant bytes in Bitcoin's LE storage).
    for (size_t i = 31; i < 32; --i)
    {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return false;
}

// ============================================================================
// Config
// ============================================================================

OkaneConfig LoadConfigCSV(const char* path)
{
    OkaneConfig cfg{};
    if (!path || path[0] == '\0') return cfg;

    std::ifstream file(path);
    if (!file) return cfg;

    std::string line;
    bool skippedHeader = false;
    while (std::getline(file, line))
    {
        if (!skippedHeader) { skippedHeader = true; continue; }
        size_t comma = line.find(',');
        if (comma == std::string::npos) continue;

        std::string key = TrimWhitespace(line.substr(0, comma));
        std::string val = TrimWhitespace(line.substr(comma + 1));

        if (key == "pool_url")           cfg.poolUrl = val;
        else if (key == "pool_url_ssl")  cfg.poolUrlSsl = val;
        else if (key == "pool_url_alt1") cfg.poolUrlAlt1 = val;
        else if (key == "pool_url_alt2") cfg.poolUrlAlt2 = val;
        else if (key == "pool_url_alt3") cfg.poolUrlAlt3 = val;
        else if (key == "pool_ports")    cfg.poolPorts = val;
        else if (key == "wallet_address") cfg.walletAddr = val;
        else if (key == "worker_name")   cfg.workerName = val;
        else if (key == "username")      cfg.username = val;
        else if (key == "password")      cfg.password = val;
    }
    return cfg;
}

// ============================================================================
// Block building
// ============================================================================

Header80 BuildBlockHeader(const BlockTemplate& tmpl)
{
    Header80 hdr{};
    WriteLE32(hdr.data() + 0, static_cast<uint32_t>(tmpl.version));
    std::memcpy(hdr.data() + 4, tmpl.previousBlock.data(), 32);

    // Merkle root: for a single coinbase tx, use the tx hash from template.
    if (!tmpl.transactions.empty())
    {
        std::memcpy(hdr.data() + 36, tmpl.transactions[0].txHash.data(), 32);
    }

    WriteLE32(hdr.data() + 68, tmpl.currentTime);
    WriteLE32(hdr.data() + 72, ReadLE32(tmpl.bits.data()));
    WriteLE32(hdr.data() + 76, tmpl.nonceRange.start);
    return hdr;
}

std::vector<uint8_t> BuildCoinbaseTx(const BlockTemplate& tmpl)
{
    std::vector<uint8_t> buf;
    buf.reserve(128);

    // version (4 bytes LE)
    AppendLE32(buf, 1);
    // input count: 1
    buf.push_back(1);
    // prev outpoint: null (32 zero bytes + 0xffffffff index)
    buf.insert(buf.end(), 32, 0);
    AppendLE32(buf, 0xFFFFFFFF);
    // coinbase script: OP_0
    buf.push_back(1); // script length
    buf.push_back(0); // OP_0
    // sequence
    AppendLE32(buf, 0xFFFFFFFF);
    // output count: 1
    buf.push_back(1);
    // value
    AppendLE64(buf, tmpl.coinbaseValue);
    // script_pubkey: OP_TRUE (placeholder)
    buf.push_back(1);    // script length
    buf.push_back(0x51); // OP_TRUE
    // locktime
    AppendLE32(buf, 0);

    return buf;
}

std::string FormatHashRate(double rate)
{
    char buf[64];
    if (rate < 1'000.0)
        sprintf_s(buf, "%.2f H/s", rate);
    else if (rate < 1'000'000.0)
        sprintf_s(buf, "%.2f KH/s", rate / 1'000.0);
    else if (rate < 1'000'000'000.0)
        sprintf_s(buf, "%.2f MH/s", rate / 1'000'000.0);
    else
        sprintf_s(buf, "%.2f GH/s", rate / 1'000'000'000.0);
    return buf;
}

// ============================================================================
// JSON-RPC Client (WinHTTP)
// ============================================================================

OkaneRpcClient::OkaneRpcClient(const std::string& url,
                                const std::string& username,
                                const std::string& password)
    : m_url(url), m_username(username), m_password(password)
{
}

OkaneRpcClient::~OkaneRpcClient() = default;

std::string OkaneRpcClient::CallRpc(const std::string& method,
                                     const std::string& params,
                                     std::string& outError)
{
    // Build JSON-RPC 1.0 request body.
    std::string body = "{\"jsonrpc\":\"1.0\",\"id\":\"okaneizer\",\"method\":\"" + method + "\"";
    if (!params.empty())
        body += ",\"params\":" + params;
    body += "}";

    // Parse URL to extract host, port, path, and scheme.
    // Expected formats:
    //   http://host:port/path   (Bitcoin Core RPC)
    //   https://host:port/path
    std::string urlCopy = m_url;

    // Stratum URLs cannot serve JSON-RPC for getblocktemplate.
    // Reject them with a clear error instead of silently timing out.
    if (urlCopy.find("stratum+tcp://") == 0 || urlCopy.find("stratum+ssl://") == 0 ||
        urlCopy.find("stratum://") == 0)
    {
        outError = "stratum URLs are not supported for getblocktemplate RPC; "
                   "use a Bitcoin Core http:// or https:// RPC endpoint";
        return {};
    }

    bool useSSL = (urlCopy.find("https://") == 0);
    size_t hostStart = urlCopy.find("://");
    if (hostStart == std::string::npos) { outError = "invalid url"; return {}; }
    hostStart += 3;

    size_t pathStart = urlCopy.find('/', hostStart);
    std::string hostPort = (pathStart != std::string::npos)
        ? urlCopy.substr(hostStart, pathStart - hostStart)
        : urlCopy.substr(hostStart);
    std::string path = (pathStart != std::string::npos) ? urlCopy.substr(pathStart) : "/";

    std::string host = hostPort;
    int port = useSSL ? 443 : 80;
    size_t colonPos = hostPort.find(':');
    if (colonPos != std::string::npos)
    {
        host = hostPort.substr(0, colonPos);
        port = std::atoi(hostPort.substr(colonPos + 1).c_str());
    }

    // Convert host to wide string for WinHTTP.
    std::wstring wHost(host.begin(), host.end());
    std::wstring wPath(path.begin(), path.end());

    HINTERNET hSession = WinHttpOpen(L"OkaneMiner/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { outError = "WinHttpOpen failed (err=" + std::to_string(GetLastError()) + ")"; return {}; }

    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(),
        static_cast<INTERNET_PORT>(port), 0);
    if (!hConnect)
    {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hSession);
        outError = "WinHttpConnect failed (err=" + std::to_string(err) + ")";
        return {};
    }

    DWORD flags = useSSL ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", wPath.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest)
    {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "WinHttpOpenRequest failed (err=" + std::to_string(err) + ")";
        return {};
    }

    // Set timeouts (10 seconds for resolve/connect/send/receive).
    WinHttpSetTimeouts(hRequest, 10000, 10000, 10000, 10000);

    // Add Content-Type and auth headers directly.
    // We build the Authorization header manually instead of using
    // WinHttpSetCredentials, because the latter relies on a 401
    // challenge-response round-trip that some RPC servers don't support
    // cleanly (they may close the connection after the 401).
    std::wstring allHeaders = L"Content-Type: application/json\r\n";
    if (!m_username.empty())
    {
        // Base64-encode "user:pass".
        std::string plain = m_username + ":" + m_password;
        static const char b64[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        encoded.reserve((plain.size() + 2) / 3 * 4);
        for (size_t i = 0; i < plain.size(); i += 3)
        {
            uint32_t n = static_cast<uint8_t>(plain[i]) << 16;
            if (i + 1 < plain.size()) n |= static_cast<uint8_t>(plain[i + 1]) << 8;
            if (i + 2 < plain.size()) n |= static_cast<uint8_t>(plain[i + 2]);
            encoded.push_back(b64[(n >> 18) & 0x3F]);
            encoded.push_back(b64[(n >> 12) & 0x3F]);
            encoded.push_back((i + 1 < plain.size()) ? b64[(n >> 6) & 0x3F] : '=');
            encoded.push_back((i + 2 < plain.size()) ? b64[n & 0x3F] : '=');
        }
        std::string authHeader = "Authorization: Basic " + encoded + "\r\n";
        allHeaders += std::wstring(authHeader.begin(), authHeader.end());
    }

    WinHttpAddRequestHeaders(hRequest, allHeaders.c_str(),
        static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    // For self-signed certs on test nodes, optionally ignore cert errors.
    if (useSSL)
    {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA
                       | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
                       | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    BOOL ok = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        const_cast<char*>(body.c_str()), static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()), 0);
    if (!ok)
    {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "WinHttpSendRequest failed (err=" + std::to_string(err) + ")";
        return {};
    }

    ok = WinHttpReceiveResponse(hRequest, nullptr);
    if (!ok)
    {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "WinHttpReceiveResponse failed (err=" + std::to_string(err) + ")";
        return {};
    }

    // Read response body.
    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0)
    {
        std::vector<char> chunk(bytesAvailable);
        DWORD bytesRead = 0;
        WinHttpReadData(hRequest, chunk.data(), bytesAvailable, &bytesRead);
        responseBody.append(chunk.data(), bytesRead);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    // Check for RPC-level error.
    std::string rpcErr = JsonGetError(responseBody);
    if (!rpcErr.empty())
    {
        outError = rpcErr;
        return {};
    }

    return JsonGetResult(responseBody);
}

bool OkaneRpcClient::GetBlockTemplate(BlockTemplate& outTemplate, std::string& outError)
{
    std::string params = "[{\"rules\":[\"segwit\"],\"capabilities\":[\"coinbase/append\",\"longpoll\"]}]";
    std::string result = CallRpc("getblocktemplate", params, outError);
    if (result.empty()) return false;

    // Parse the template JSON object.
    outTemplate.version = static_cast<int32_t>(JsonGetInt(result, "version"));
    outTemplate.currentTime = static_cast<uint32_t>(JsonGetUInt64(result, "curtime"));
    outTemplate.coinbaseValue = JsonGetUInt64(result, "coinbasevalue");
    outTemplate.longpollId = JsonGetString(result, "longpollid");

    std::string prevHash = JsonGetString(result, "previousblockhash");
    if (!DecodeHex(prevHash, outTemplate.previousBlock.data(), 32))
    {
        outError = "bad previousblockhash";
        return false;
    }

    std::string targetHex = JsonGetString(result, "target");
    if (!DecodeHex(targetHex, outTemplate.target.data(), 32))
    {
        outError = "bad target";
        return false;
    }

    std::string bitsHex = JsonGetString(result, "bits");
    if (!DecodeHex(bitsHex, outTemplate.bits.data(), 4))
    {
        outError = "bad bits";
        return false;
    }

    std::string nonceRangeHex = JsonGetString(result, "noncerange");
    if (nonceRangeHex.size() == 16)
    {
        uint8_t nrBytes[8];
        DecodeHex(nonceRangeHex, nrBytes, 8);
        outTemplate.nonceRange.start = ReadLE32(nrBytes);
        outTemplate.nonceRange.end   = ReadLE32(nrBytes + 4);
    }
    else
    {
        outTemplate.nonceRange.start = 0;
        outTemplate.nonceRange.end   = 0xFFFFFFFF;
    }

    // TODO: parse transactions array for full merkle tree support.
    // For solo mining with a single coinbase, this is sufficient.

    return true;
}

bool OkaneRpcClient::SubmitBlock(const std::string& blockHex, std::string& outError)
{
    std::string params = "[\"" + blockHex + "\"]";
    CallRpc("submitblock", params, outError);
    return outError.empty();
}

bool OkaneRpcClient::GetNewAddress(std::string& outAddr, std::string& outError)
{
    std::string result = CallRpc("getnewaddress", "", outError);
    if (!outError.empty()) return false;
    outAddr = result;
    return true;
}

// ============================================================================
// Stratum v1 Client
// ============================================================================

bool IsStratumUrl(const std::string& url)
{
    return url.find("stratum+tcp://") == 0 ||
           url.find("stratum+ssl://") == 0 ||
           url.find("stratum://") == 0;
}

namespace
{

// Winsock init helper — calls WSAStartup once.
struct WsaInit
{
    WsaInit()
    {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    ~WsaInit() { WSACleanup(); }
};
static WsaInit s_wsaInit;

// Minimal JSON helpers for stratum (line-delimited JSON, no nesting depth > 2).
std::string StratumJsonGetString(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    ++pos;
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return {};
    return json.substr(pos, end - pos);
}

int64_t StratumJsonGetInt(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return 0;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    return std::strtoll(json.c_str() + pos, nullptr, 10);
}

double StratumJsonGetDouble(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0.0;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return 0.0;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    return std::strtod(json.c_str() + pos, nullptr);
}

// Get the "result" or "error" from a stratum JSON-RPC response.
// Stratum uses JSON-RPC: {"id":N, "result":..., "error":...}
bool StratumGetBool(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && json[pos] == ' ') ++pos;
    return (pos < json.size() && json[pos] == 't');
}

// Extract the "result" field value as raw JSON substring.
std::string StratumGetResultRaw(const std::string& json)
{
    size_t pos = json.find("\"result\"");
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + 8);
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos >= json.size()) return {};

    // Array
    if (json[pos] == '[')
    {
        int depth = 0;
        size_t start = pos;
        for (; pos < json.size(); ++pos)
        {
            if (json[pos] == '[') ++depth;
            else if (json[pos] == ']') { --depth; if (depth == 0) return json.substr(start, pos - start + 1); }
        }
        return {};
    }
    // true/false/null/number
    size_t start = pos;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}') ++pos;
    return TrimWhitespace(json.substr(start, pos - start));
}

// Extract error message from stratum response.
std::string StratumGetError(const std::string& json)
{
    size_t pos = json.find("\"error\"");
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + 7);
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos < json.size() && json[pos] == 'n') return {}; // null
    // Array format: [code, "message", null]
    if (json[pos] == '[')
    {
        size_t msgStart = json.find('"', pos + 1);
        if (msgStart == std::string::npos) return "unknown error";
        ++msgStart;
        size_t msgEnd = json.find('"', msgStart);
        if (msgEnd == std::string::npos) return "unknown error";
        return json.substr(msgStart, msgEnd - msgStart);
    }
    return {};
}

// Parse a JSON array of strings, e.g. ["abc","def","ghi"]
std::vector<std::string> ParseStringArray(const std::string& json)
{
    std::vector<std::string> result;
    size_t pos = 0;
    while ((pos = json.find('"', pos)) != std::string::npos)
    {
        ++pos;
        size_t end = json.find('"', pos);
        if (end == std::string::npos) break;
        result.push_back(json.substr(pos, end - pos));
        pos = end + 1;
    }
    return result;
}

// Reverse byte order of a hex string (swap pairs).  Used for prevhash.
std::string ReverseHexBytes(const std::string& hex)
{
    std::string out;
    out.reserve(hex.size());
    for (size_t i = hex.size(); i >= 2; i -= 2)
        out.append(hex, i - 2, 2);
    return out;
}

} // anonymous namespace (stratum helpers)

OkaneStratumClient::OkaneStratumClient(const std::string& url,
                                       const std::string& username,
                                       const std::string& password)
    : m_url(url), m_user(username), m_pass(password)
{
    // Initialize target to difficulty 1.
    m_target.fill(0);
    m_target[27] = 0xFF;
    m_target[26] = 0xFF;
    // diff 1 target = 0x00000000FFFF0000...00 (big endian)
    // In LE storage: target[26]=0xFF, target[27]=0xFF, rest=0
}

OkaneStratumClient::~OkaneStratumClient()
{
    Disconnect();
}

bool OkaneStratumClient::Connect(std::string& outError)
{
    Disconnect();

    // Parse URL: stratum+tcp://host:port
    std::string urlCopy = m_url;
    size_t schemeEnd = urlCopy.find("://");
    if (schemeEnd == std::string::npos) { outError = "invalid stratum url"; return false; }
    std::string hostPort = urlCopy.substr(schemeEnd + 3);

    // Strip trailing path if any.
    size_t slashPos = hostPort.find('/');
    if (slashPos != std::string::npos) hostPort = hostPort.substr(0, slashPos);

    std::string host = hostPort;
    std::string port = "3333"; // default stratum port
    size_t colonPos = hostPort.find(':');
    if (colonPos != std::string::npos)
    {
        host = hostPort.substr(0, colonPos);
        port = hostPort.substr(colonPos + 1);
    }

    // Resolve and connect.
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (rc != 0 || !result)
    {
        outError = "DNS resolve failed for " + host + ":" + port + " (err=" + std::to_string(rc) + ")";
        if (result) freeaddrinfo(result);
        return false;
    }

    m_sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (m_sock == INVALID_SOCKET)
    {
        outError = "socket() failed (err=" + std::to_string(WSAGetLastError()) + ")";
        freeaddrinfo(result);
        return false;
    }

    // Non-blocking connect with 5-second timeout to fail fast on unreachable pools.
    {
        u_long nonBlock = 1;
        ioctlsocket(m_sock, FIONBIO, &nonBlock);

        rc = connect(m_sock, result->ai_addr, static_cast<int>(result->ai_addrlen));
        freeaddrinfo(result);

        if (rc == SOCKET_ERROR)
        {
            int wsaErr = WSAGetLastError();
            if (wsaErr != WSAEWOULDBLOCK)
            {
                outError = "connect() failed to " + host + ":" + port + " (err=" + std::to_string(wsaErr) + ")";
                closesocket(m_sock);
                m_sock = INVALID_SOCKET;
                return false;
            }

            // Wait for connection to complete.
            fd_set writeSet, errorSet;
            FD_ZERO(&writeSet);
            FD_ZERO(&errorSet);
            FD_SET(m_sock, &writeSet);
            FD_SET(m_sock, &errorSet);
            timeval tv;
            tv.tv_sec = 5;
            tv.tv_usec = 0;

            int selRc = select(0, nullptr, &writeSet, &errorSet, &tv);
            if (selRc <= 0 || FD_ISSET(m_sock, &errorSet))
            {
                int sockErr = 0;
                int optLen = sizeof(sockErr);
                getsockopt(m_sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&sockErr), &optLen);
                outError = "connect() timed out or failed to " + host + ":" + port +
                           " (err=" + std::to_string(sockErr) + ")";
                closesocket(m_sock);
                m_sock = INVALID_SOCKET;
                return false;
            }
        }

        // Restore blocking mode.
        u_long blocking = 0;
        ioctlsocket(m_sock, FIONBIO, &blocking);
    }

    // Set recv timeout.
    DWORD timeout = 15000;
    setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    m_connected.store(true);
    m_recvBuf.clear();

    // --- mining.subscribe ---
    {
        std::string req = "{\"id\":" + std::to_string(m_nextId++) +
            ",\"method\":\"mining.subscribe\",\"params\":[\"OkaneMiner/1.0\"]}\n";
        if (!SendLine(req)) { outError = "failed to send mining.subscribe"; Disconnect(); return false; }

        std::string resp;
        if (!RecvLine(resp, 15000)) { outError = "no response to mining.subscribe"; Disconnect(); return false; }

        std::string err = StratumGetError(resp);
        if (!err.empty()) { outError = "mining.subscribe error: " + err; Disconnect(); return false; }

        // Parse result: [[["mining.set_difficulty","sub_id"],["mining.notify","sub_id"]],"extranonce1",extranonce2_size]
        std::string resultRaw = StratumGetResultRaw(resp);
        // Extract extranonce1: find the hex string after subscription arrays.
        // Look for the pattern "],"<hex>",<int>]
        // Simple approach: find extranonce1 by looking for the second top-level comma in result.
        size_t firstComma = 0;
        int bracketDepth = 0;
        for (size_t i = 0; i < resultRaw.size(); ++i)
        {
            if (resultRaw[i] == '[') ++bracketDepth;
            else if (resultRaw[i] == ']') --bracketDepth;
            else if (resultRaw[i] == ',' && bracketDepth == 1)
            {
                if (firstComma == 0) { firstComma = i; break; }
            }
        }
        if (firstComma > 0)
        {
            // After firstComma: ,"extranonce1",size]
            size_t q1 = resultRaw.find('"', firstComma);
            if (q1 != std::string::npos)
            {
                size_t q2 = resultRaw.find('"', q1 + 1);
                if (q2 != std::string::npos)
                    m_extranonce1 = resultRaw.substr(q1 + 1, q2 - q1 - 1);

                // extranonce2_size is after next comma.
                size_t nextComma = resultRaw.find(',', q2);
                if (nextComma != std::string::npos)
                    m_extranonce2Size = std::atoi(resultRaw.c_str() + nextComma + 1);
            }
        }

        if (m_extranonce1.empty())
        {
            outError = "could not parse extranonce1 from subscribe response";
            Disconnect();
            return false;
        }

        OutputDebugStringA(("[okane][stratum] subscribed extranonce1=" + m_extranonce1 +
                            " en2size=" + std::to_string(m_extranonce2Size) + "\n").c_str());
    }

    // --- mining.authorize ---
    {
        std::string req = "{\"id\":" + std::to_string(m_nextId++) +
            ",\"method\":\"mining.authorize\",\"params\":[\"" +
            m_user + "\",\"" + m_pass + "\"]}\n";
        if (!SendLine(req)) { outError = "failed to send mining.authorize"; Disconnect(); return false; }

        std::string resp;
        if (!RecvLine(resp, 15000)) { outError = "no response to mining.authorize"; Disconnect(); return false; }

        // Check for error.
        std::string err = StratumGetError(resp);
        if (!err.empty()) { outError = "mining.authorize error: " + err; Disconnect(); return false; }

        // Also check for set_difficulty or notify that might arrive before auth response.
        // Process any mining.set_difficulty or mining.notify that arrived.
        if (resp.find("\"mining.set_difficulty\"") != std::string::npos)
        {
            // Parse difficulty from params: [diff]
            size_t paramsPos = resp.find("\"params\"");
            if (paramsPos != std::string::npos)
            {
                size_t bracket = resp.find('[', paramsPos);
                if (bracket != std::string::npos)
                {
                    double diff = std::strtod(resp.c_str() + bracket + 1, nullptr);
                    if (diff > 0.0) SetDifficulty(diff);
                }
            }
            // Read actual auth response.
            if (!RecvLine(resp, 10000)) { outError = "no auth response after set_difficulty"; Disconnect(); return false; }
        }

        OutputDebugStringA("[okane][stratum] authorized\n");
    }

    // --- mining.suggest_difficulty (after handshake) ---
    // Request a low share difficulty so we can submit shares at Xbox GPU hash rates.
    // Sent after subscribe+authorize so it doesn't interfere with the handshake.
    {
        std::string req = "{\"id\":" + std::to_string(m_nextId++) +
            ",\"method\":\"mining.suggest_difficulty\",\"params\":[1]}\n";
        SendLine(req); // Best-effort; response processed by RecvLoop.
    }

    return true;
}

void OkaneStratumClient::Disconnect()
{
    m_stopFlag.store(true);
    m_connected.store(false);

    if (m_sock != INVALID_SOCKET)
    {
        shutdown(m_sock, SD_BOTH);
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }

    if (m_recvThread.joinable()) m_recvThread.join();

    // Wake anyone waiting on job.
    m_jobCv.notify_all();
    m_stopFlag.store(false);
}

bool OkaneStratumClient::IsConnected() const
{
    return m_connected.load();
}

void OkaneStratumClient::StartReceiving()
{
    m_stopFlag.store(false);
    m_recvThread = std::thread(&OkaneStratumClient::RecvLoop, this);
}

bool OkaneStratumClient::WaitForJob(StratumJob& outJob, double timeoutSec)
{
    std::unique_lock<std::mutex> lock(m_jobMu);
    bool got = m_jobCv.wait_for(lock, std::chrono::milliseconds(static_cast<int>(timeoutSec * 1000)),
        [this]() { return m_hasNewJob || m_stopFlag.load() || !m_connected.load(); });

    if (!got || m_stopFlag.load() || !m_connected.load()) return false;
    outJob = m_latestJob;
    m_hasNewJob = false;
    return true;
}

bool OkaneStratumClient::HasNewJob() const
{
    std::lock_guard<std::mutex> lock(m_jobMu);
    return m_hasNewJob;
}

bool OkaneStratumClient::Submit(const std::string& jobId,
                                const std::string& extranonce2Hex,
                                const std::string& ntimeHex,
                                const std::string& nonceHex,
                                std::string& outError)
{
    std::string req = "{\"id\":" + std::to_string(m_nextId++) +
        ",\"method\":\"mining.submit\",\"params\":[\"" +
        m_user + "\",\"" + jobId + "\",\"" +
        extranonce2Hex + "\",\"" + ntimeHex + "\",\"" + nonceHex + "\"]}\n";

    if (!SendLine(req)) { outError = "failed to send mining.submit"; return false; }
    // Response is processed asynchronously by RecvLoop.
    return true;
}

Hash32 OkaneStratumClient::GetTarget() const
{
    std::lock_guard<std::mutex> lock(m_diffMu);
    return m_target;
}

std::string OkaneStratumClient::GetExtranonce1() const
{
    return m_extranonce1;
}

int OkaneStratumClient::GetExtranonce2Size() const
{
    return m_extranonce2Size;
}

bool OkaneStratumClient::SendLine(const std::string& json)
{
    if (m_sock == INVALID_SOCKET) return false;
    int sent = send(m_sock, json.c_str(), static_cast<int>(json.size()), 0);
    return sent == static_cast<int>(json.size());
}

bool OkaneStratumClient::RecvLine(std::string& out, int timeoutMs)
{
    // Check buffer first for a complete line.
    auto processBuffer = [&]() -> bool {
        size_t nl = m_recvBuf.find('\n');
        if (nl != std::string::npos)
        {
            out = m_recvBuf.substr(0, nl);
            m_recvBuf.erase(0, nl + 1);
            // Trim trailing \r.
            if (!out.empty() && out.back() == '\r') out.pop_back();
            return true;
        }
        return false;
    };

    if (processBuffer()) return true;

    // Set timeout.
    DWORD to = static_cast<DWORD>(timeoutMs);
    setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));

    char buf[4096];
    auto start = std::chrono::steady_clock::now();
    while (true)
    {
        int n = recv(m_sock, buf, sizeof(buf), 0);
        if (n > 0)
        {
            m_recvBuf.append(buf, n);
            if (processBuffer()) return true;
        }
        else
        {
            // Error or timeout.
            return false;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeoutMs) return false;
    }
}

void OkaneStratumClient::RecvLoop()
{
    // Set a reasonable recv timeout so we can check stop flag periodically.
    DWORD timeout = 2000;
    setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    char buf[4096];
    while (!m_stopFlag.load() && m_connected.load())
    {
        int n = recv(m_sock, buf, sizeof(buf), 0);
        if (n > 0)
        {
            m_recvBuf.append(buf, n);

            // Process complete lines.
            size_t nl;
            while ((nl = m_recvBuf.find('\n')) != std::string::npos)
            {
                std::string line = m_recvBuf.substr(0, nl);
                m_recvBuf.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;

                // Dispatch by method.
                if (line.find("\"mining.notify\"") != std::string::npos)
                {
                    // Parse mining.notify params.
                    // params: [job_id, prevhash, coinb1, coinb2, merkle_branches[], version, nbits, ntime, clean_jobs]
                    size_t paramsPos = line.find("\"params\"");
                    if (paramsPos == std::string::npos) continue;
                    size_t bracket = line.find('[', paramsPos);
                    if (bracket == std::string::npos) continue;

                    // Extract params array content.
                    // Find matching ] at depth 1 (there's a nested array for merkle branches).
                    size_t arrStart = bracket + 1;
                    int depth = 1;
                    size_t arrEnd = arrStart;
                    for (; arrEnd < line.size(); ++arrEnd)
                    {
                        if (line[arrEnd] == '[') ++depth;
                        else if (line[arrEnd] == ']') { --depth; if (depth == 0) break; }
                    }
                    std::string paramsContent = line.substr(arrStart, arrEnd - arrStart);

                    // Parse fields by extracting quoted strings and values sequentially.
                    std::vector<std::string> fields;
                    size_t p = 0;
                    while (p < paramsContent.size() && fields.size() < 9)
                    {
                        while (p < paramsContent.size() && (paramsContent[p] == ' ' || paramsContent[p] == ',')) ++p;
                        if (p >= paramsContent.size()) break;

                        if (paramsContent[p] == '"')
                        {
                            ++p;
                            size_t end = paramsContent.find('"', p);
                            if (end == std::string::npos) break;
                            fields.push_back(paramsContent.substr(p, end - p));
                            p = end + 1;
                        }
                        else if (paramsContent[p] == '[')
                        {
                            // Merkle branches array.
                            size_t mbEnd = paramsContent.find(']', p);
                            if (mbEnd == std::string::npos) break;
                            std::string mbContent = paramsContent.substr(p + 1, mbEnd - p - 1);
                            fields.push_back(mbContent); // store raw, parse below
                            p = mbEnd + 1;
                        }
                        else
                        {
                            // boolean or number
                            size_t end = paramsContent.find_first_of(",]", p);
                            if (end == std::string::npos) end = paramsContent.size();
                            fields.push_back(TrimWhitespace(paramsContent.substr(p, end - p)));
                            p = end;
                        }
                    }

                    if (fields.size() >= 9)
                    {
                        StratumJob job;
                        job.jobId = fields[0];

                        // prevhash: 64 hex chars, already in internal byte order from pool.
                        if (fields[1].size() == 64)
                            DecodeHex(fields[1], job.prevHash.data(), 32);

                        job.coinb1 = fields[2];
                        job.coinb2 = fields[3];

                        // Merkle branches: parse from raw string.
                        job.merkleBranches = ParseStringArray(fields[4]);

                        // version: 8 hex chars (big-endian)
                        if (fields[5].size() == 8)
                        {
                            uint8_t vBytes[4];
                            DecodeHex(fields[5], vBytes, 4);
                            job.version = static_cast<int32_t>(
                                (static_cast<uint32_t>(vBytes[0]) << 24) |
                                (static_cast<uint32_t>(vBytes[1]) << 16) |
                                (static_cast<uint32_t>(vBytes[2]) << 8) |
                                static_cast<uint32_t>(vBytes[3]));
                        }

                        // nbits: 8 hex chars
                        if (fields[6].size() == 8)
                            DecodeHex(fields[6], job.nbits.data(), 4);

                        // ntime: 8 hex chars (big-endian uint32)
                        if (fields[7].size() == 8)
                        {
                            uint8_t tBytes[4];
                            DecodeHex(fields[7], tBytes, 4);
                            job.ntime = (static_cast<uint32_t>(tBytes[0]) << 24) |
                                        (static_cast<uint32_t>(tBytes[1]) << 16) |
                                        (static_cast<uint32_t>(tBytes[2]) << 8) |
                                        static_cast<uint32_t>(tBytes[3]);
                        }

                        // clean_jobs
                        job.cleanJobs = (fields[8] == "true");

                        {
                            std::lock_guard<std::mutex> lock(m_jobMu);
                            m_latestJob = std::move(job);
                            m_hasNewJob = true;
                        }
                        m_jobCv.notify_one();

                        OutputDebugStringA(("[okane][stratum] notify job=" + fields[0] + "\n").c_str());
                    }
                }
                else if (line.find("\"mining.set_difficulty\"") != std::string::npos)
                {
                    size_t paramsPos = line.find("\"params\"");
                    if (paramsPos != std::string::npos)
                    {
                        size_t bracket = line.find('[', paramsPos);
                        if (bracket != std::string::npos)
                        {
                            double diff = std::strtod(line.c_str() + bracket + 1, nullptr);
                            if (diff > 0.0)
                            {
                                SetDifficulty(diff);
                                OutputDebugStringA(("[okane][stratum] set_difficulty=" +
                                    std::to_string(diff) + "\n").c_str());
                            }
                        }
                    }
                }
                else if (line.find("\"result\"") != std::string::npos)
                {
                    // RPC response (to submit, etc.) — log if error.
                    std::string err = StratumGetError(line);
                    if (!err.empty())
                        OutputDebugStringA(("[okane][stratum] rpc error: " + err + "\n").c_str());
                }
            }
        }
        else if (n == 0)
        {
            // Connection closed.
            OutputDebugStringA("[okane][stratum] connection closed by server\n");
            m_connected.store(false);
            m_jobCv.notify_all();
            return;
        }
        else
        {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) continue; // Normal timeout, check stop flag.
            OutputDebugStringA(("[okane][stratum] recv error=" + std::to_string(err) + "\n").c_str());
            m_connected.store(false);
            m_jobCv.notify_all();
            return;
        }
    }
}

void OkaneStratumClient::SetDifficulty(double diff)
{
    std::lock_guard<std::mutex> lock(m_diffMu);
    m_difficulty = diff;
    m_target = DifficultyToTarget(diff);
}

Hash32 OkaneStratumClient::DifficultyToTarget(double diff) const
{
    // Difficulty 1 target (Bitcoin): 0x00000000FFFF0000...00 (256-bit big-endian)
    // target = diff1_target / diff
    // We compute this in big-endian then store in LE order for our HashLessThan.

    // diff1 numerator = 0xFFFF * 2^208
    // target = (0xFFFF * 2^208) / diff
    // Working in double: find leading bytes, then fill the 32-byte target.

    Hash32 target{};
    if (diff <= 0.0) { target.fill(0xFF); return target; }

    // Use the standard bdiff formula:
    // target[256-bit] = (2^224 - 1) * 0x10000 / diff   (approximately)
    // More precisely: diff1 = 0x00000000FFFF << 208
    // We'll compute byte-by-byte from MSB.

    double numerator = 0xFFFF;
    // We need target as 256-bit number where diff1 has 0x0000FFFF at bytes [4..5] (big-endian).
    // target_bigendian[i] where i=0 is MSB.
    // diff1 big-endian: 00 00 00 00 FF FF 00 00 ... 00 (32 bytes)
    //   bytes 4,5 = FF,FF; rest = 0
    // target = diff1 / diff

    // Simple approach: compute as double, place bytes.
    double val = numerator / diff;
    // val represents the value at byte position 4..5 (with 26 zero bytes below = 208 bits).
    // So the full target = val * 2^208.

    // Convert to a 32-byte big-endian number.
    // Start from the most significant affected byte.
    uint8_t be[32] = {};

    // val is the 16-bit-ish value at bytes 4-5.  But it could be larger (low diff)
    // or much smaller (high diff).  We need to express val * 2^208 as a 256-bit number.

    // log2(val * 2^208) = log2(val) + 208
    // position of MSB in bits from the top: 256 - (log2(val) + 208) - 1
    // byte offset from top: floor(that / 8)

    if (val >= 1.0)
    {
        // Place val starting at byte 5 (LSB of the 0xFFFF pair at bytes [4,5]).
        // val could be > 65535 for difficulties < 1.
        double v = val;
        int bytePos = 5; // LSB position of diff1 0xFFFF in big-endian
        while (v >= 256.0 && bytePos > 0)
        {
            --bytePos;
            v /= 256.0;
        }
        for (int i = bytePos; i < 32 && v > 0.0; ++i)
        {
            uint8_t b = static_cast<uint8_t>(v);
            be[i] = b;
            v = (v - b) * 256.0;
        }
    }
    else
    {
        // val < 1 means high difficulty; shift right from byte 5.
        double v = val;
        int bytePos = 5;
        while (v < 1.0 && bytePos < 31)
        {
            ++bytePos;
            v *= 256.0;
        }
        for (int i = bytePos; i < 32 && v > 0.0; ++i)
        {
            uint8_t b = static_cast<uint8_t>(v);
            be[i] = b;
            v = (v - b) * 256.0;
        }
    }

    // Our HashLessThan compares from byte[31] down (big-endian MSB is at high index in LE).
    // Convert big-endian to our LE storage: reverse.
    for (int i = 0; i < 32; ++i)
        target[i] = be[31 - i];

    return target;
}

OkaneMiner::OkaneMiner(OkaneRpcClient* rpc,
                       const std::string& walletAddr,
                       int workers,
                       OkaneGpuHasher* gpu)
    : m_rpc(rpc)
    , m_walletAddr(walletAddr)
    , m_workers(workers > 0 ? workers : static_cast<int>(std::thread::hardware_concurrency()))
    , m_gpu(gpu)
{
    m_status.walletAddr = walletAddr;
    m_status.useGpu = (gpu != nullptr);
}

OkaneMiner::OkaneMiner(OkaneStratumClient* stratum,
                       const std::string& workerName,
                       int workers,
                       OkaneGpuHasher* gpu)
    : m_stratum(stratum)
    , m_walletAddr(workerName)
    , m_workers(workers > 0 ? workers : static_cast<int>(std::thread::hardware_concurrency()))
    , m_gpu(gpu)
{
    m_status.walletAddr = workerName;
    m_status.useGpu = (gpu != nullptr);
}

OkaneMiner::~OkaneMiner()
{
    Stop();
}

void OkaneMiner::Start()
{
    std::lock_guard<std::mutex> lock(m_mu);
    if (m_running) return;

    m_running = true;
    m_stopFlag.store(false);
    m_status.running = true;
    m_status.lastError.clear();
    m_hashCount.store(0);

    if (m_stratum)
        m_mineThread = std::thread(&OkaneMiner::StratumMineLoop, this);
    else
        m_mineThread = std::thread(&OkaneMiner::MineLoop, this);
    m_rateThread = std::thread(&OkaneMiner::RateLoop, this);
}

void OkaneMiner::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mu);
        if (!m_running) return;
        m_stopFlag.store(true);
        m_running = false;
        m_status.running = false;
    }

    if (m_mineThread.joinable()) m_mineThread.join();
    if (m_rateThread.joinable()) m_rateThread.join();
}

OkaneMinerSnapshot OkaneMiner::GetStatus() const
{
    std::lock_guard<std::mutex> lock(m_mu);
    return m_status;
}

void OkaneMiner::RateLoop()
{
    uint64_t lastCount = 0;
    while (!m_stopFlag.load())
    {
        // Sleep 2 seconds, checking stop flag every 100ms.
        for (int i = 0; i < 20 && !m_stopFlag.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (m_stopFlag.load()) return;

        uint64_t count = m_hashCount.load();
        uint64_t delta = count - lastCount;
        lastCount = count;
        double rate = static_cast<double>(delta) / 2.0;

        std::lock_guard<std::mutex> lock(m_mu);
        m_status.hashRate = rate;
        m_status.hashRateText = FormatHashRate(rate);
    }
}

void OkaneMiner::MineLoop()
{
    while (!m_stopFlag.load())
    {
        BlockTemplate tmpl;
        std::string err;
        if (!m_rpc->GetBlockTemplate(tmpl, err))
        {
            {
                std::lock_guard<std::mutex> lock(m_mu);
                m_status.lastError = "getblocktemplate: " + err;
            }
            OutputDebugStringA(("[okane][miner] getblocktemplate error: " + err + "\n").c_str());

            // Retry after 5 seconds.
            for (int i = 0; i < 50 && !m_stopFlag.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::string blockHex;
        bool found = MineBlock(tmpl, blockHex);
        if (!found) continue;

        OutputDebugStringA("[okane][miner] block found! submitting...\n");
        err.clear();
        if (!m_rpc->SubmitBlock(blockHex, err))
        {
            std::lock_guard<std::mutex> lock(m_mu);
            m_status.lastError = "submitblock: " + err;
            OutputDebugStringA(("[okane][miner] submitblock error: " + err + "\n").c_str());
        }
        else
        {
            std::lock_guard<std::mutex> lock(m_mu);
            m_status.blocksFound++;
            m_status.lastError.clear();
            OutputDebugStringA("[okane][miner] block accepted!\n");
        }
    }

    std::lock_guard<std::mutex> lock(m_mu);
    m_running = false;
    m_status.running = false;
}

bool OkaneMiner::MineBlock(const BlockTemplate& tmpl, std::string& outBlockHex)
{
    Header80 header = BuildBlockHeader(tmpl);
    const Hash32& target = tmpl.target;

    // Try GPU first.
    if (m_gpu && m_gpu->IsValid())
    {
        Header80 winHeader{};
        if (m_gpu->Process(header.data(), target.data(), winHeader.data()))
        {
            outBlockHex = BuildBlock(tmpl, winHeader);
            return true;
        }
    }

    // ---- Midstate optimization ----
    // SHA-256 processes 64-byte blocks.  The 80-byte header's first 64 bytes
    // are constant across nonces.  Compute the midstate once, then each worker
    // only needs to hash the second 16-byte block (+ padding).
    Sha256State midstate = Sha256Midstate(header.data());

    // Prepare the second SHA-256 block's message schedule (W).
    // Only W[3] (the nonce) changes per attempt.
    uint32_t templateW[64];
    PrepareSecondBlockW(templateW, header.data());

    uint32_t nonceStart = tmpl.nonceRange.start;
    uint32_t nonceEnd   = tmpl.nonceRange.end;
    uint32_t total = nonceEnd - nonceStart;
    uint32_t chunkSize = total / static_cast<uint32_t>(m_workers);

    struct WorkerResult { uint32_t nonce; bool found; };
    std::atomic<bool> foundFlag{false};
    std::mutex resultMu;
    WorkerResult bestResult{0, false};

    std::vector<std::thread> workers;
    workers.reserve(m_workers);

    constexpr uint32_t BATCH = 2048;

    for (int w = 0; w < m_workers; ++w)
    {
        uint32_t lo = nonceStart + static_cast<uint32_t>(w) * chunkSize;
        uint32_t hi = (static_cast<size_t>(w) == static_cast<size_t>(m_workers) - 1) ? nonceEnd : (lo + chunkSize);

        workers.emplace_back([&, lo, hi, midstate]()
        {
            uint32_t W[64];
            std::memcpy(W, templateW, sizeof(templateW));

            uint32_t localCount = 0;
            for (uint32_t nonce = lo; nonce < hi; ++nonce)
            {
                // Check stop/found every BATCH hashes.
                if ((localCount & (BATCH - 1)) == 0 && localCount > 0)
                {
                    m_hashCount.fetch_add(BATCH, std::memory_order_relaxed);
                    if (m_stopFlag.load(std::memory_order_relaxed) ||
                        foundFlag.load(std::memory_order_relaxed)) return;
                }
                ++localCount;

                // Set nonce in W[3] (big-endian).
                W[3] = (nonce >> 24) | ((nonce >> 8) & 0xFF00) |
                       ((nonce << 8) & 0xFF0000) | (nonce << 24);

                // Re-expand W[18..63] (W[16],W[17] are precomputed).
                ExpandW_FromNonce(W);

                // First SHA-256: midstate + second block.
                Sha256State s1 = midstate;
                Sha256TransformW(s1, W);

                // Quick reject: if h[0] != 0, hash is way above any target.
                if (QuickRejectHash(s1)) continue;

                // Second SHA-256: hash the 32-byte first hash.
                uint32_t W2[64];
                PrepareHashBlockW(W2, s1);
                Sha256State s2 = SHA256_INIT;
                Sha256TransformW(s2, W2);

                // Convert to Hash32 (big-endian output → our LE storage).
                Hash32 hash{};
                for (int i = 0; i < 8; ++i)
                    WriteBE32(hash.data() + i * 4, s2.h[i]);

                if (HashLessThan(hash, target))
                {
                    bool expected = false;
                    if (foundFlag.compare_exchange_strong(expected, true))
                    {
                        std::lock_guard<std::mutex> lock(resultMu);
                        bestResult = {nonce, true};
                    }
                    m_hashCount.fetch_add(localCount & (BATCH - 1), std::memory_order_relaxed);
                    return;
                }
            }
            // Flush remaining count.
            m_hashCount.fetch_add(localCount & (BATCH - 1), std::memory_order_relaxed);
        });
    }

    for (auto& t : workers)
        t.join();

    if (!bestResult.found) return false;

    // Reconstruct with winning nonce.
    WriteLE32(header.data() + 76, bestResult.nonce);
    outBlockHex = BuildBlock(tmpl, header);
    return true;
}

std::string OkaneMiner::BuildBlock(const BlockTemplate& tmpl, const Header80& header)
{
    auto coinbaseTx = BuildCoinbaseTx(tmpl);

    std::vector<uint8_t> blockBytes;
    blockBytes.reserve(80 + 1 + coinbaseTx.size());
    blockBytes.insert(blockBytes.end(), header.begin(), header.end());
    blockBytes.push_back(1); // varint: 1 transaction
    blockBytes.insert(blockBytes.end(), coinbaseTx.begin(), coinbaseTx.end());

    return EncodeHex(blockBytes.data(), blockBytes.size());
}

// ============================================================================
// Stratum mining
// ============================================================================

void OkaneMiner::StratumMineLoop()
{
    m_stratum->StartReceiving();

    while (!m_stopFlag.load())
    {
        if (!m_stratum->IsConnected())
        {
            {
                std::lock_guard<std::mutex> lock(m_mu);
                m_status.lastError = "stratum disconnected, reconnecting...";
            }
            OutputDebugStringA("[okane][miner] stratum disconnected, reconnecting...\n");

            std::string err;
            if (!m_stratum->Connect(err))
            {
                {
                    std::lock_guard<std::mutex> lock(m_mu);
                    m_status.lastError = "reconnect failed: " + err;
                }
                OutputDebugStringA(("[okane][miner] reconnect failed: " + err + "\n").c_str());

                for (int i = 0; i < 50 && !m_stopFlag.load(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            m_stratum->StartReceiving();
        }

        StratumJob job;
        if (!m_stratum->WaitForJob(job, 30.0))
        {
            if (m_stopFlag.load()) break;
            OutputDebugStringA("[okane][miner] WaitForJob timed out, retrying...\n");
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(m_mu);
            m_status.lastError.clear();
        }

        Hash32 target = m_stratum->GetTarget();
        std::string extranonce1 = m_stratum->GetExtranonce1();
        int en2Size = m_stratum->GetExtranonce2Size();

        OutputDebugStringA(("[okane][miner] mining job=" + job.jobId + " en1=" + extranonce1
            + " en2sz=" + std::to_string(en2Size) + " gpu=" + (m_gpu ? "yes" : "no") + "\n").c_str());

        std::string en2Hex, ntimeHex, nonceHex;
        bool found = MineStratumJob(job, target, extranonce1, en2Size, en2Hex, ntimeHex, nonceHex);

        {
            // Log hash rate from RateLoop data.
            std::lock_guard<std::mutex> lock(m_mu);
            char rateBuf[128];
            sprintf_s(rateBuf, "[okane][miner] dispatch done found=%s rate=%s hashes=%" PRIu64 "\n",
                      found ? "YES" : "no", m_status.hashRateText.c_str(), m_hashCount.load());
            OutputDebugStringA(rateBuf);
        }

        if (!found) continue;

        OutputDebugStringA("[okane][miner] share found! submitting...\n");
        std::string err;
        if (!m_stratum->Submit(job.jobId, en2Hex, ntimeHex, nonceHex, err))
        {
            std::lock_guard<std::mutex> lock(m_mu);
            m_status.lastError = "submit: " + err;
            OutputDebugStringA(("[okane][miner] submit error: " + err + "\n").c_str());
        }
        else
        {
            std::lock_guard<std::mutex> lock(m_mu);
            m_status.blocksFound++;
            m_status.lastError.clear();
            OutputDebugStringA("[okane][miner] share submitted\n");
        }
    }

    std::lock_guard<std::mutex> lock(m_mu);
    m_running = false;
    m_status.running = false;
}

bool OkaneMiner::MineStratumJob(const StratumJob& job,
                                const Hash32& target,
                                const std::string& extranonce1,
                                int extranonce2Size,
                                std::string& outExtranonce2,
                                std::string& outNtime,
                                std::string& outNonce)
{
    // Build coinbase: coinb1 + extranonce1 + extranonce2 + coinb2
    // We iterate extranonce2 as a simple counter (starting from 0).
    // For each extranonce2, iterate all nonces.

    // For simplicity, try one extranonce2 value (0), scan full nonce range.
    // This gives 2^32 attempts per job, which is standard.

    // Build extranonce2 hex (zero-padded).
    std::string en2Hex(static_cast<size_t>(extranonce2Size) * 2, '0');

    // Build coinbase transaction bytes.
    std::string coinbaseHex = job.coinb1 + extranonce1 + en2Hex + job.coinb2;
    std::vector<uint8_t> coinbaseBytes(coinbaseHex.size() / 2);
    DecodeHex(coinbaseHex, coinbaseBytes.data(), coinbaseBytes.size());

    // Double-SHA256 the coinbase to get its txid.
    Hash32 cbHash = DoubleSHA256(coinbaseBytes.data(), coinbaseBytes.size());

    // Compute merkle root by combining with branches.
    Hash32 merkleRoot = cbHash;
    for (const auto& branchHex : job.merkleBranches)
    {
        if (branchHex.size() != 64) continue;
        Hash32 branch{};
        DecodeHex(branchHex, branch.data(), 32);

        // Concatenate merkleRoot + branch, double-hash.
        uint8_t combined[64];
        std::memcpy(combined, merkleRoot.data(), 32);
        std::memcpy(combined + 32, branch.data(), 32);
        merkleRoot = DoubleSHA256(combined, 64);
    }

    // Build the 80-byte block header.
    // Stratum convention: hex-decoded fields are placed as raw bytes (big-endian integers).
    // This matches how the pool reconstructs the header for share verification.
    Header80 header{};
    WriteBE32(header.data() + 0, static_cast<uint32_t>(job.version));

    // Prevhash from stratum: hex-decoded bytes placed directly.
    std::memcpy(header.data() + 4, job.prevHash.data(), 32);

    std::memcpy(header.data() + 36, merkleRoot.data(), 32);
    WriteBE32(header.data() + 68, job.ntime);
    std::memcpy(header.data() + 72, job.nbits.data(), 4);
    // nonce at offset 76 — set per attempt.

    // Format ntime as hex for submission.
    {
        char buf[9];
        sprintf_s(buf, "%08x", job.ntime);
        outNtime = buf;
    }
    outExtranonce2 = en2Hex;

    // ---- Midstate optimization ----
    Sha256State midstate = Sha256Midstate(header.data());
    uint32_t templateW[64];
    PrepareSecondBlockW(templateW, header.data());

    // ==== GPU PATH ====
    if (m_gpu && m_gpu->IsValid())
    {
        GpuMineParams params{};
        for (int i = 0; i < 8; ++i)
            params.midstate[i] = midstate.h[i];

        params.secondBlockW[0] = templateW[0];
        params.secondBlockW[1] = templateW[1];
        params.secondBlockW[2] = templateW[2];

        Hash32 tgt = m_stratum->GetTarget();
        std::memcpy(params.target, tgt.data(), 32);

        // Each dispatch hashes NUM_THREADS (1,048,576) nonces.
        // Loop over the full 4B nonce space in batches.
        constexpr uint32_t kThreadsPerBatch = 4096u * 256u; // must match shader
        uint32_t nonce = 0;

        OutputDebugStringA("[okane][miner] GPU mining started\n");

        try
        {
        for (;;)
        {
            params.nonceStart = nonce;

            GpuMineResult result{};
            if (!m_gpu->ProcessMidstate(params, result))
            {
                OutputDebugStringA("[okane][miner] GPU dispatch failed\n");
                return false;
            }

            m_hashCount.fetch_add(kThreadsPerBatch, std::memory_order_relaxed);

            if (result.found)
            {
                char msg[128];
                sprintf_s(msg, "[okane][miner] share found by GPU nonce=0x%08x\n", result.nonce);
                OutputDebugStringA(msg);

                char buf[9];
                sprintf_s(buf, "%08x", result.nonce);
                outNonce = buf;
                return true;
            }

            // Check if we should abort (stop flag, disconnect, or new job arrived).
            if (m_stopFlag.load(std::memory_order_relaxed))
                return false;
            if (m_stratum && (!m_stratum->IsConnected() || m_stratum->HasNewJob()))
            {
                OutputDebugStringA("[okane][miner] aborting GPU scan — new job or disconnect\n");
                return false;
            }

            // Check for overflow — we've scanned the entire nonce space.
            if (nonce > 0xFFFFFFFFu - kThreadsPerBatch)
                break;
            nonce += kThreadsPerBatch;

            // Log progress every ~256M hashes (every 256 batches).
            if ((nonce & 0x0FFFFFFF) == 0)
            {
                char prog[128];
                sprintf_s(prog, "[okane][miner] GPU progress: nonce=0x%08x hashes=%" PRIu64 "\n",
                          nonce, m_hashCount.load());
                OutputDebugStringA(prog);
            }
        }
        }
        catch (const std::exception& ex)
        {
            OutputDebugStringA(("[okane][miner] GPU exception: " + std::string(ex.what()) + "\n").c_str());
            return false;
        }
        catch (...)
        {
            OutputDebugStringA("[okane][miner] GPU unknown exception\n");
            return false;
        }

        OutputDebugStringA("[okane][miner] GPU exhausted nonce space\n");
        return false;
    }

    // ==== CPU PATH (fallback) ====
    uint32_t nonceStart = 0;
    uint32_t nonceEnd = 0xFFFFFFFF;
    uint32_t total = nonceEnd - nonceStart;
    uint32_t chunkSize = total / static_cast<uint32_t>(m_workers);

    struct WorkerResult { uint32_t nonce; bool found; };
    std::atomic<bool> foundFlag{false};
    std::mutex resultMu;
    WorkerResult bestResult{0, false};

    std::vector<std::thread> workers;
    workers.reserve(m_workers);

    constexpr uint32_t BATCH = 2048;

    for (int w = 0; w < m_workers; ++w)
    {
        uint32_t lo = nonceStart + static_cast<uint32_t>(w) * chunkSize;
        uint32_t hi = (static_cast<size_t>(w) == static_cast<size_t>(m_workers) - 1) ? nonceEnd : (lo + chunkSize);

        workers.emplace_back([&, lo, hi, midstate]()
        {
            uint32_t W[64];
            std::memcpy(W, templateW, sizeof(templateW));

            uint32_t localCount = 0;
            for (uint32_t nonce = lo; nonce < hi; ++nonce)
            {
                if ((localCount & (BATCH - 1)) == 0 && localCount > 0)
                {
                    m_hashCount.fetch_add(BATCH, std::memory_order_relaxed);
                    if (m_stopFlag.load(std::memory_order_relaxed) ||
                        foundFlag.load(std::memory_order_relaxed)) return;
                }
                ++localCount;

                // Nonce goes directly into W[3] as big-endian (matching header byte order).
                W[3] = nonce;
                ExpandW_FromNonce(W);

                Sha256State s1 = midstate;
                Sha256TransformW(s1, W);

                uint32_t W2[64];
                PrepareHashBlockW(W2, s1);
                Sha256State s2 = SHA256_INIT;
                Sha256TransformW(s2, W2);

                // Quick reject: if final hash h[0] != 0, the first 4 BE bytes are
                // nonzero, so hash > any target at difficulty >= 1.
                if (QuickRejectHash(s2)) continue;

                // Write hash in little-endian for HashLessThan comparison.
                Hash32 hash{};
                for (int i = 0; i < 8; ++i)
                    WriteLE32(hash.data() + (7 - i) * 4, s2.h[i]);

                if (HashLessThan(hash, target))
                {
                    bool expected = false;
                    if (foundFlag.compare_exchange_strong(expected, true))
                    {
                        std::lock_guard<std::mutex> lk(resultMu);
                        bestResult = {nonce, true};
                    }
                    m_hashCount.fetch_add(localCount & (BATCH - 1), std::memory_order_relaxed);
                    return;
                }
            }
            m_hashCount.fetch_add(localCount & (BATCH - 1), std::memory_order_relaxed);
        });
    }

    for (auto& t : workers)
        t.join();

    if (!bestResult.found) return false;

    // Format nonce as hex (big-endian for stratum submission).
    {
        char buf[9];
        sprintf_s(buf, "%08x", bestResult.nonce);
        outNonce = buf;
    }

    return true;
}
