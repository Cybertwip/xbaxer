// SHA256Compute.hlsl — GPU double-SHA256 mining kernel (midstate-optimized)
//
// Aggressive version for Xbox XDK (44 CUs, RDNA2).
// Uses CPU-supplied midstate to skip the first 64 bytes of SHA-256.
// Only computes second block (nonce variation) + second SHA-256 pass.
//
// Buffers:
//   t0 (SRV) : inputData   – 80 bytes laid out as:
//                             [0..31]  midstate (8 x uint32, big-endian SHA state)
//                             [32..43] secondBlock W[0..2] (merkle_tail, ntime, nbits)
//                             [44..47] nonceStart (uint32)
//                             [48..79] padding/unused
//   t1 (SRV) : inputTarget – 32 bytes (8 x uint, LE storage matching CPU Hash32)
//   u0 (UAV) : outputData  – 8 bytes: [0..3] found flag, [4..7] winning nonce

ByteAddressBuffer   inputData   : register(t0);
ByteAddressBuffer   inputTarget : register(t1);
RWByteAddressBuffer outputData  : register(u0);

#define WORKGROUP_SIZE 256
#define NUM_WORKGROUPS 4096
#define NUM_THREADS    (WORKGROUP_SIZE * NUM_WORKGROUPS)

// ---- SHA-256 helpers -------------------------------------------------------

uint RotR(uint x, uint n) { return (x >> n) | (x << (32u - n)); }

uint Sigma0(uint x) { return RotR(x,  2u) ^ RotR(x, 13u) ^ RotR(x, 22u); }
uint Sigma1(uint x) { return RotR(x,  6u) ^ RotR(x, 11u) ^ RotR(x, 25u); }
uint Gamma0(uint x) { return RotR(x,  7u) ^ RotR(x, 18u) ^ (x >>  3u); }
uint Gamma1(uint x) { return RotR(x, 17u) ^ RotR(x, 19u) ^ (x >> 10u); }
uint Maj(uint a, uint b, uint c) { return (a & b) ^ (a & c) ^ (b & c); }
uint Ch (uint e, uint f, uint g) { return (e & f) ^ ((~e) & g); }

static const uint K[64] =
{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// ---- SHA-256 compression (64 rounds, state in/out) -------------------------

void SHA256_Transform(inout uint state[8], uint w[64])
{
    uint a = state[0], b = state[1], c = state[2], d = state[3];
    uint e = state[4], f = state[5], g = state[6], h = state[7];

    [unroll] for (uint j = 0; j < 64; j++)
    {
        uint t2 = Sigma0(a) + Maj(a, b, c);
        uint t1 = h + Sigma1(e) + Ch(e, f, g) + K[j] + w[j];
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

// ---- target comparison (LE storage — compare from MSB at index 7) ----------

bool MeetsTarget(uint hash[8], uint targ[8])
{
    // hash and target are stored in little-endian uint32 order.
    // hash[7] contains the most-significant 4 bytes.
    [unroll] for (int i = 7; i >= 0; i--)
    {
        if (hash[i] < targ[i]) return true;
        if (hash[i] > targ[i]) return false;
    }
    return true;
}

// ---- shared midstate (computed once per workgroup) -------------------------

groupshared uint gs_midstate[8];
groupshared uint gs_W012[3];   // W[0], W[1], W[2] of second block
groupshared uint gs_target[8];
groupshared uint gs_nonceStart;
// Pre-expanded W values that don't depend on nonce
groupshared uint gs_W16;
groupshared uint gs_W17;

// ---- entry point -----------------------------------------------------------

#define COMPUTE_RS "SRV(t0), SRV(t1), UAV(u0)"

[RootSignature(COMPUTE_RS)]
[numthreads(WORKGROUP_SIZE, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    // Thread 0 of each group loads shared data from the SRV.
    if (gi == 0)
    {
        [unroll] for (uint i = 0; i < 8; i++)
            gs_midstate[i] = inputData.Load(i * 4);

        gs_W012[0] = inputData.Load(32);  // merkle_tail (header bytes 64-67)
        gs_W012[1] = inputData.Load(36);  // ntime       (header bytes 68-71)
        gs_W012[2] = inputData.Load(40);  // nbits       (header bytes 72-75)
        gs_nonceStart = inputData.Load(44);

        [unroll] for (uint t = 0; t < 8; t++)
            gs_target[t] = inputTarget.Load(t * 4);

        // Pre-expand W[16] and W[17] which don't depend on nonce (W[3]).
        // Second block layout:
        // W[0]=merkle_tail, W[1]=ntime, W[2]=nbits, W[3]=nonce
        // W[4]=0x80000000, W[5..14]=0, W[15]=640
        // W[16] = Gamma1(W[14]) + W[9] + Gamma0(W[1]) + W[0]
        //       = Gamma1(0)     + 0    + Gamma0(ntime) + merkle_tail
        gs_W16 = Gamma0(gs_W012[1]) + gs_W012[0];
        // W[17] = Gamma1(W[15]) + W[10] + Gamma0(W[2]) + W[1]
        //       = Gamma1(640)   + 0     + Gamma0(nbits) + ntime
        gs_W17 = Gamma1(640u) + Gamma0(gs_W012[2]) + gs_W012[1];
    }
    GroupMemoryBarrierWithGroupSync();

    // Load from shared memory into registers.
    uint midstate[8];
    [unroll] for (uint mi = 0; mi < 8; mi++)
        midstate[mi] = gs_midstate[mi];

    uint sW0 = gs_W012[0];
    uint sW1 = gs_W012[1];
    uint sW2 = gs_W012[2];

    uint target[8];
    [unroll] for (uint ti = 0; ti < 8; ti++)
        target[ti] = gs_target[ti];

    uint nonce = gs_nonceStart + dtid.x;
    uint preW16 = gs_W16;
    uint preW17 = gs_W17;

    // Each thread hashes exactly ONE nonce per dispatch.
    // CPU loops over nonce ranges by updating nonceStart between dispatches.
    {
        // --- First SHA-256 pass: second block (bytes 64-79 + padding) ---
        uint s1[8];
        [unroll] for (uint c = 0; c < 8; c++)
            s1[c] = midstate[c];

        uint w[64];
        w[0] = sW0;
        w[1] = sW1;
        w[2] = sW2;
        w[3] = nonce;
        w[4] = 0x80000000u;
        [unroll] for (uint z = 5; z < 15; z++) w[z] = 0;
        w[15] = 640u;

        w[16] = preW16;
        w[17] = preW17;
        w[18] = Gamma1(w[16]) + w[11] + Gamma0(w[3]) + w[2];
        w[19] = Gamma1(w[17]) + w[12] + Gamma0(w[4]) + w[3];
        [unroll] for (uint e = 20; e < 64; e++)
            w[e] = w[e - 16] + Gamma0(w[e - 15]) + w[e - 7] + Gamma1(w[e - 2]);

        SHA256_Transform(s1, w);

        // --- Second SHA-256 pass ---
        uint w2[64];
        [unroll] for (uint si = 0; si < 8; si++) w2[si] = s1[si];
        w2[8]  = 0x80000000u;
        [unroll] for (uint z2 = 9; z2 < 15; z2++) w2[z2] = 0;
        w2[15] = 256u;
        [unroll] for (uint e2 = 16; e2 < 64; e2++)
            w2[e2] = w2[e2 - 16] + Gamma0(w2[e2 - 15]) + w2[e2 - 7] + Gamma1(w2[e2 - 2]);

        uint s2[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
        SHA256_Transform(s2, w2);

        if (s2[0] == 0)
        {
            uint hashLE[8];
            [unroll] for (uint r = 0; r < 8; r++)
                hashLE[r] = s2[7 - r];

            if (MeetsTarget(hashLE, target))
            {
                uint prev;
                outputData.InterlockedCompareExchange(0, 0, 1, prev);
                if (prev == 0)
                    outputData.Store(4, nonce);
            }
        }
    }
}
