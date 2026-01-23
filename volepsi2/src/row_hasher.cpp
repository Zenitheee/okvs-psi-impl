#include "okvs/row_hasher.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <cstring>
#include <wmmintrin.h>
#include <emmintrin.h>
#include <smmintrin.h>

namespace okvs {

namespace {

// Macro or template to handle immediate argument for aeskeygenassist
template<int RCON>
__m128i aes_expand(__m128i key) {
    __m128i keygen = _mm_aeskeygenassist_si128(key, RCON);
    keygen = _mm_shuffle_epi32(keygen, 0xff);
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    return _mm_xor_si128(key, keygen);
}

} // namespace

RowHasher::RowHasher(std::size_t mPrime, std::size_t denseCols, std::size_t weight, std::uint64_t seed)
    : mMPrime(mPrime),
      mDenseCols(denseCols),
      mWeight(weight) {
    
    // Expand AES key derived from seed
    __uint128_t key = 0;
    // Put seed in key
    std::memcpy(&key, &seed, sizeof(seed));
    
    // Simple key expansion
    __m128i k = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&key));
    mRoundKeys[0] = k;
    mRoundKeys[1] = k = aes_expand<0x01>(k);
    mRoundKeys[2] = k = aes_expand<0x02>(k);
    mRoundKeys[3] = k = aes_expand<0x04>(k);
    mRoundKeys[4] = k = aes_expand<0x08>(k);
    mRoundKeys[5] = k = aes_expand<0x10>(k);
    mRoundKeys[6] = k = aes_expand<0x20>(k);
    mRoundKeys[7] = k = aes_expand<0x40>(k);
    mRoundKeys[8] = k = aes_expand<0x80>(k);
    mRoundKeys[9] = k = aes_expand<0x1b>(k);
    mRoundKeys[10] = k = aes_expand<0x36>(k);
}

RowData RowHasher::generate(std::span<const std::uint8_t> keyBytes) const {
    RowData row;
    row.sparse.reserve(mWeight);
    if (mDenseCols > 0) {
        row.dense.resize(mDenseCols);
    }

    // 1. Hash the keyBytes to a 128-bit block to be used as plaintext
    // Simple mixing if keyBytes is not 16 bytes.
    // Ideally we assume keyBytes is 16 bytes.
    __m128i block = _mm_setzero_si128();
    if (keyBytes.size() == 16) {
        block = _mm_loadu_si128(reinterpret_cast<const __m128i*>(keyBytes.data()));
    } else {
        // Fallback or simple hash.
        // For now, copy what fits.
        std::size_t n = std::min(keyBytes.size(), std::size_t(16));
        std::memcpy(&block, keyBytes.data(), n);
    }

    // 2. Encrypt the block to get randomness
    // AES-ENC
    __m128i state = _mm_xor_si128(block, mRoundKeys[0]);
    for (int i = 1; i < 10; ++i) {
        state = _mm_aesenc_si128(state, mRoundKeys[i]);
    }
    state = _mm_aesenclast_si128(state, mRoundKeys[10]);

    // state now contains 128 bits of pseudo-randomness
    
    // 3. Extract sparse indices
    // We need 'mWeight' distinct indices.
    // Assuming mWeight is small (e.g. 3).
    // We can interpret the 128-bit output as a sequence of 32-bit integers.
    // But we need to ensure they are distinct.
    
    uint64_t randLow = _mm_cvtsi128_si64(state);
    uint64_t randHigh = _mm_extract_epi64(state, 1);
    
    // Strategy: Use 32-bit chunks.
    // Note: This simple extraction might be biased if mMPrime is not power of 2.
    // For high performance, we accept slight bias or use lemire's fastrange/rejection.
    // Given the constraints, simple modulo is often "good enough" for benchmarks,
    // but we should check duplicates.
    
    // Re-hash if we run out of entropy or need more?
    // For weight=3, we need 3 indices.
    // We have 4x32 bits.
    
    std::uint32_t r[4];
    r[0] = static_cast<std::uint32_t>(randLow);
    r[1] = static_cast<std::uint32_t>(randLow >> 32);
    r[2] = static_cast<std::uint32_t>(randHigh);
    r[3] = static_cast<std::uint32_t>(randHigh >> 32);
    
    int found = 0;
    int attempt = 0;
    
    // To handle collisions efficiently without loop:
    // Try first 3. If distinct, good.
    // If collision, swap with 4th.
    // If still collision, re-encrypt state (as a PRNG step).
    
    // Simplified robust loop:
    while (found < mWeight) {
        for (int i = 0; i < 4 && found < mWeight; ++i) {
            uint32_t candidate = r[i] % mMPrime;
            bool duplicate = false;
            for (int k = 0; k < found; ++k) {
                if (row.sparse[k] == candidate) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                row.sparse.push_back(candidate);
                found++;
            }
        }
        
        if (found < mWeight) {
            // Need more randomness
            state = _mm_aesenc_si128(state, mRoundKeys[0]); // Re-encrypt state as next random block
            randLow = _mm_cvtsi128_si64(state);
            randHigh = _mm_extract_epi64(state, 1);
            r[0] = static_cast<std::uint32_t>(randLow);
            r[1] = static_cast<std::uint32_t>(randLow >> 32);
            r[2] = static_cast<std::uint32_t>(randHigh);
            r[3] = static_cast<std::uint32_t>(randHigh >> 32);
        }
    }
    std::sort(row.sparse.begin(), row.sparse.end());

    // 4. Generate Dense Part
    if (mDenseCols > 0) {
        // We need a base for the powers.
        // Use the randomness we have.
        // If we consumed all 4 u32s, we might need new randomness.
        // But likely we have enough bits left in 'state' if we just need 128 bits for base.
        
        // Let's just generate a fresh random 128-bit block for the dense base to be safe and uniform.
        // Or if we want to be very fast, we use the current 'state' if it hasn't been exhausted.
        // To be safe and high quality:
        
        // Use the original 'state' (the one from key) + 1 (counter mode) or similar?
        // Let's just encrypt 'state' again.
        
        __m128i denseBase = _mm_aesenc_si128(state, mRoundKeys[1]); // Next random
        
        // Ensure non-zero
        // If zero (extremely unlikely), try again.
        while (_mm_test_all_zeros(denseBase, denseBase)) {
             denseBase = _mm_aesenc_si128(denseBase, mRoundKeys[2]);
        }

        GF128 base;
        // Hack: access private members or use helper. 
        // We need to construct GF128 from __m128i.
        // We can cast.
        // But GF128 stores as array<uint64_t, 2>.
        // We can implement a fast constructor or just use fromUint128.
        
        uint64_t dLow = _mm_cvtsi128_si64(denseBase);
        uint64_t dHigh = _mm_extract_epi64(denseBase, 1);
        base = GF128(dHigh, dLow);

        // Optimization: Precompute powers? Or just multiply.
        // Since denseCols is small (~40-60), doing 40 muls is okay-ish.
        // But we can do better if we pipeline.
        
        GF128 power = base;
        for (std::size_t i = 0; i < mDenseCols; ++i) {
            row.dense[i] = power;
            power *= base;
        }
    }

    return row;
}

} // namespace okvs

