#include "okvs/row_hasher.h"

#include "hash_utils.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <emmintrin.h>
#include <smmintrin.h>
#include <wmmintrin.h>

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

__m128i aes_encrypt(__m128i block, const std::array<AesRoundKey, 11>& roundKeys) {
    __m128i state = _mm_xor_si128(block, roundKeys[0].value);
    for (int i = 1; i < 10; ++i) {
        state = _mm_aesenc_si128(state, roundKeys[i].value);
    }
    return _mm_aesenclast_si128(state, roundKeys[10].value);
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
    mRoundKeys[0].value = k;
    mRoundKeys[1].value = k = aes_expand<0x01>(k);
    mRoundKeys[2].value = k = aes_expand<0x02>(k);
    mRoundKeys[3].value = k = aes_expand<0x04>(k);
    mRoundKeys[4].value = k = aes_expand<0x08>(k);
    mRoundKeys[5].value = k = aes_expand<0x10>(k);
    mRoundKeys[6].value = k = aes_expand<0x20>(k);
    mRoundKeys[7].value = k = aes_expand<0x40>(k);
    mRoundKeys[8].value = k = aes_expand<0x80>(k);
    mRoundKeys[9].value = k = aes_expand<0x1b>(k);
    mRoundKeys[10].value = k = aes_expand<0x36>(k);
}

RowData RowHasher::generate(std::span<const std::uint8_t> keyBytes) const {
    if (mMPrime != 0 && mMPrime < mWeight) {
        throw std::runtime_error("Sparse column count smaller than row weight.");
    }
    if (mMPrime > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Sparse column count exceeds 32-bit row index representation.");
    }

    RowData row;
    row.sparse.reserve(mWeight);
    if (mDenseCols > 0) {
        row.dense.resize(mDenseCols);
    }

    // 1. Compress arbitrary-length key material to one pseudorandom 128-bit block.
    const auto words = internal::hashBytesTo128(
        keyBytes,
        0x6a09e667f3bcc909ULL,
        0xbb67ae8584caa73bULL,
        internal::HashDomain::RowKeyCompression);
    alignas(16) std::array<std::uint64_t, 2> hashedBlock = {words[0], words[1]};
    __m128i block = _mm_load_si128(reinterpret_cast<const __m128i*>(hashedBlock.data()));

    // 2. Encrypt the block to get randomness.
    __m128i state = aes_encrypt(block, mRoundKeys);

    // 3. Extract sparse indices as an unbiased sample without replacement.
    std::array<std::uint32_t, 4> randomWords{};
    std::size_t randomWordIndex = randomWords.size();
    auto refillRandomWords = [&]() {
        const std::uint64_t randLow = _mm_cvtsi128_si64(state);
        const std::uint64_t randHigh = _mm_extract_epi64(state, 1);
        randomWords[0] = static_cast<std::uint32_t>(randLow);
        randomWords[1] = static_cast<std::uint32_t>(randLow >> 32);
        randomWords[2] = static_cast<std::uint32_t>(randHigh);
        randomWords[3] = static_cast<std::uint32_t>(randHigh >> 32);
        randomWordIndex = 0;
        state = aes_encrypt(state, mRoundKeys);
    };
    auto nextUint32 = [&]() -> std::uint32_t {
        if (randomWordIndex == randomWords.size()) {
            refillRandomWords();
        }
        return randomWords[randomWordIndex++];
    };

    if (mMPrime != 0) {
        const auto range = static_cast<std::uint64_t>(mMPrime);
        const auto limit = (std::uint64_t{1} << 32) - ((std::uint64_t{1} << 32) % range);

        auto drawSparseIndex = [&]() -> std::uint32_t {
            while (true) {
                const auto sample = static_cast<std::uint64_t>(nextUint32());
                if (sample < limit) {
                    return static_cast<std::uint32_t>(sample % range);
                }
            }
        };

        while (row.sparse.size() < mWeight) {
            const auto candidate = drawSparseIndex();
            if (std::find(row.sparse.begin(), row.sparse.end(), candidate) == row.sparse.end()) {
                row.sparse.push_back(candidate);
            }
        }
    }
    std::sort(row.sparse.begin(), row.sparse.end());

    // 4. Generate Dense Part
    if (mDenseCols > 0) {
        // Consume a fresh AES block for the dense-row base after the sparse sample.
        __m128i denseBase = state;
        state = aes_encrypt(state, mRoundKeys);
        
        // Ensure non-zero
        // If zero (extremely unlikely), try again.
        while (_mm_test_all_zeros(denseBase, denseBase)) {
            denseBase = state;
            state = aes_encrypt(state, mRoundKeys);
        }

        const uint64_t dLow = _mm_cvtsi128_si64(denseBase);
        const uint64_t dHigh = _mm_extract_epi64(denseBase, 1);
        GF128 base(dHigh, dLow);

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
