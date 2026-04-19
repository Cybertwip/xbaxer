#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// winsock2.h is expected from pch.h before windows.h.
// Forward-declare SOCKET if not available (e.g. when included standalone).
#ifndef _WINSOCK2API_
typedef unsigned __int64 SOCKET;
#define INVALID_SOCKET (~(SOCKET)0)
#endif

// Forward-declare so callers don't need OkaneGpuHasher.h
class OkaneGpuHasher;

// ---------- Configuration ----------

struct OkaneConfig
{
    std::string poolUrl;
    std::string poolUrlSsl;
    std::string poolUrlAlt1;
    std::string poolUrlAlt2;
    std::string poolUrlAlt3;
    std::string poolPorts;
    std::string walletAddr;
    std::string workerName;
    std::string username;
    std::string password;
};

OkaneConfig LoadConfigCSV(const char* path);

// ---------- Block types ----------

using Hash32  = std::array<uint8_t, 32>;
using Hex4    = std::array<uint8_t, 4>;
using Header80 = std::array<uint8_t, 80>;

struct BlockTransaction
{
    Hash32               id{};
    std::vector<uint8_t> data;
    Hash32               txHash{};
    uint64_t             fee = 0;
    uint32_t             weight = 0;
};

struct NonceRange
{
    uint32_t start = 0;
    uint32_t end   = 0xFFFFFFFF;
};

struct BlockTemplate
{
    int32_t                       version = 0;
    Hash32                        previousBlock{};
    std::vector<BlockTransaction> transactions;
    std::string                   longpollId;
    Hash32                        target{};
    Hex4                          bits{};
    uint32_t                      currentTime = 0;
    uint64_t                      coinbaseValue = 0;
    NonceRange                    nonceRange;
};

// ---------- JSON-RPC client ----------

class OkaneRpcClient
{
public:
    OkaneRpcClient(const std::string& url,
                   const std::string& username,
                   const std::string& password);
    ~OkaneRpcClient();

    OkaneRpcClient(const OkaneRpcClient&) = delete;
    OkaneRpcClient& operator=(const OkaneRpcClient&) = delete;

    bool GetBlockTemplate(BlockTemplate& outTemplate, std::string& outError);
    bool SubmitBlock(const std::string& blockHex, std::string& outError);
    bool GetNewAddress(std::string& outAddr, std::string& outError);

private:
    std::string CallRpc(const std::string& method,
                        const std::string& params,
                        std::string& outError);

    std::string m_url;
    std::string m_username;
    std::string m_password;
};

// ---------- Stratum v1 client ----------

struct StratumJob
{
    std::string jobId;
    Hash32      prevHash{};
    std::string coinb1;           // hex
    std::string coinb2;           // hex
    std::vector<std::string> merkleBranches; // hex strings
    int32_t     version   = 0;
    Hex4        nbits{};
    uint32_t    ntime     = 0;
    bool        cleanJobs = false;
};

class OkaneStratumClient
{
public:
    OkaneStratumClient(const std::string& url,
                       const std::string& username,
                       const std::string& password);
    ~OkaneStratumClient();

    OkaneStratumClient(const OkaneStratumClient&) = delete;
    OkaneStratumClient& operator=(const OkaneStratumClient&) = delete;

    // Connect, subscribe, and authorize.  Blocks until ready or fails.
    bool Connect(std::string& outError);
    void Disconnect();
    bool IsConnected() const;

    // Start the background receive thread that processes mining.notify etc.
    void StartReceiving();

    // Block until a new job arrives (or timeout/disconnect).
    // Returns false if disconnected or stopped.
    bool WaitForJob(StratumJob& outJob, double timeoutSec = 30.0);

    // Non-blocking: returns true if a new job has arrived since last WaitForJob.
    bool HasNewJob() const;

    // Submit a share.
    bool Submit(const std::string& jobId,
                const std::string& extranonce2Hex,
                const std::string& ntimeHex,
                const std::string& nonceHex,
                std::string& outError);

    // Current share difficulty target.
    Hash32 GetTarget() const;
    std::string GetExtranonce1() const;
    int         GetExtranonce2Size() const;

private:
    bool SendLine(const std::string& json);
    bool RecvLine(std::string& out, int timeoutMs = 10000);
    void HandleLine(const std::string& line);
    void RecvLoop();
    void SetDifficulty(double diff);
    Hash32 DifficultyToTarget(double diff) const;

    std::string m_url;
    std::string m_user;
    std::string m_pass;

    SOCKET  m_sock = INVALID_SOCKET;
    int     m_nextId = 1;

    // Subscription data.
    std::string m_extranonce1;
    int         m_extranonce2Size = 4;

    // Difficulty / target.
    mutable std::mutex m_diffMu;
    double m_difficulty = 1.0;
    Hash32 m_target{};

    // Job queue (latest job only).
    mutable std::mutex      m_jobMu;
    std::condition_variable m_jobCv;
    StratumJob              m_latestJob;
    bool                    m_hasNewJob = false;

    std::thread       m_recvThread;
    std::atomic<bool> m_stopFlag{false};
    std::atomic<bool> m_connected{false};

    // Receive buffer for partial lines.
    std::string m_recvBuf;
};

// ---------- Miner status ----------

struct OkaneMinerSnapshot
{
    bool        running      = false;
    double      hashRate     = 0.0;
    std::string hashRateText;
    uint64_t    blocksFound  = 0;
    std::string walletAddr;
    bool        useGpu       = false;
    std::string lastError;
};

// ---------- Miner ----------

class OkaneMiner
{
public:
    // GBT mode (Bitcoin Core RPC).
    OkaneMiner(OkaneRpcClient* rpc,
               const std::string& walletAddr,
               int workers,
               OkaneGpuHasher* gpu);

    // Stratum mode (pool mining).
    OkaneMiner(OkaneStratumClient* stratum,
               const std::string& workerName,
               int workers,
               OkaneGpuHasher* gpu);

    ~OkaneMiner();

    OkaneMiner(const OkaneMiner&) = delete;
    OkaneMiner& operator=(const OkaneMiner&) = delete;

    void Start();
    void Stop();
    OkaneMinerSnapshot GetStatus() const;

private:
    void MineLoop();
    void StratumMineLoop();
    void RateLoop();
    bool MineBlock(const BlockTemplate& tmpl, std::string& outBlockHex);
    std::string BuildBlock(const BlockTemplate& tmpl, const Header80& header);

    // Stratum-specific mining: returns true if a valid share/block was found.
    bool MineStratumJob(const StratumJob& job,
                        const Hash32& target,
                        const std::string& extranonce1,
                        int extranonce2Size,
                        std::string& outExtranonce2,
                        std::string& outNtime,
                        std::string& outNonce);

    OkaneRpcClient*      m_rpc = nullptr;
    OkaneStratumClient*  m_stratum = nullptr;
    std::string          m_walletAddr;
    int                  m_workers;
    OkaneGpuHasher*      m_gpu;

    mutable std::mutex m_mu;
    bool               m_running = false;
    OkaneMinerSnapshot m_status;
    std::atomic<uint64_t> m_hashCount{0};

    std::thread m_mineThread;
    std::thread m_rateThread;
    std::atomic<bool> m_stopFlag{false};
};

// ---------- Helpers ----------

Hash32  DoubleSHA256(const uint8_t* data, size_t len);
bool    HashLessThan(const Hash32& hash, const Hash32& target);
Header80 BuildBlockHeader(const BlockTemplate& tmpl);
std::vector<uint8_t> BuildCoinbaseTx(const BlockTemplate& tmpl);
std::string FormatHashRate(double rate);
bool IsStratumUrl(const std::string& url);
