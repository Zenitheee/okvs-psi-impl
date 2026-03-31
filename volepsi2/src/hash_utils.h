#pragma once

#include "okvs/gf128.h"

#include <cryptoTools/Crypto/RandomOracle.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace okvs::internal {

enum class HashDomain : std::uint64_t {
    RowKeyCompression = 0x726f772d6b657931ULL,
    GenericBytesTo128 = 0x686173682d313238ULL,
    GenericFieldToField = 0x6669656c642d3266ULL,
    BinnedKey = 0x62696e2d6b657931ULL,
    PsiBaseField = 0x7073692d62617365ULL,
    PsiOutput = 0x7073692d6f757470ULL,
};

inline std::uint64_t splitMix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

template<std::size_t OutputBytes>
std::array<std::uint8_t, OutputBytes> hashWithDomain(std::span<const std::uint8_t> bytes,
                                                     std::uint64_t seed0,
                                                     std::uint64_t seed1,
                                                     HashDomain domain) {
    static_assert(OutputBytes > 0, "hash output must be non-empty");
    static_assert(OutputBytes <= osuCrypto::RandomOracle::MaxHashSize,
                  "requested hash output is too large");

    osuCrypto::RandomOracle ro(OutputBytes);
    const auto domainTag = static_cast<std::uint64_t>(domain);
    const auto byteCount = static_cast<std::uint64_t>(bytes.size());

    ro.Update(&domainTag, 1);
    ro.Update(&seed0, 1);
    ro.Update(&seed1, 1);
    ro.Update(&byteCount, 1);
    if (!bytes.empty()) {
        ro.Update(bytes.data(), static_cast<osuCrypto::u64>(bytes.size()));
    }

    std::array<std::uint8_t, OutputBytes> out{};
    ro.Final(out.data());
    return out;
}

inline std::array<std::uint64_t, 2> hashBytesTo128(std::span<const std::uint8_t> bytes,
                                                   std::uint64_t seed0,
                                                   std::uint64_t seed1,
                                                   HashDomain domain = HashDomain::GenericBytesTo128) {
    const auto digest = hashWithDomain<16>(bytes, seed0, seed1, domain);
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
    for (int i = 0; i < 8; ++i) {
        lo |= static_cast<std::uint64_t>(digest[i]) << (8 * i);
        hi |= static_cast<std::uint64_t>(digest[8 + i]) << (8 * i);
    }
    return {lo, hi};
}

inline std::uint64_t hashBytesToU64(std::span<const std::uint8_t> bytes,
                                    std::uint64_t seed0,
                                    std::uint64_t seed1,
                                    HashDomain domain) {
    const auto digest = hashWithDomain<8>(bytes, seed0, seed1, domain);
    std::uint64_t out = 0;
    for (int i = 0; i < 8; ++i) {
        out |= static_cast<std::uint64_t>(digest[i]) << (8 * i);
    }
    return out;
}

inline GF128 hashBytesToField(std::span<const std::uint8_t> bytes,
                              std::uint64_t seed0,
                              std::uint64_t seed1,
                              HashDomain domain = HashDomain::GenericBytesTo128) {
    const auto words = hashBytesTo128(bytes, seed0, seed1, domain);
    return GF128(words[1], words[0]);
}

inline GF128 hashFieldToField(const GF128& value,
                              std::uint64_t seed0,
                              std::uint64_t seed1,
                              HashDomain domain = HashDomain::GenericFieldToField) {
    const auto bytes = value.toBytes();
    return hashBytesToField(
        std::span<const std::uint8_t>(bytes.data(), bytes.size()),
        seed0,
        seed1,
        domain);
}

} // namespace okvs::internal
