#pragma once

#include "okvs/gf128.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace okvs::internal {

inline std::uint64_t splitMix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

inline std::uint64_t absorbBytes(std::uint64_t state,
                                 std::span<const std::uint8_t> bytes,
                                 std::uint64_t multiplier) {
    for (std::uint8_t byte : bytes) {
        state ^= static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ULL;
        state *= multiplier;
        state = std::rotl(state, 27);
        state *= 0x3c79ac492ba7b653ULL;
    }
    return state;
}

inline std::array<std::uint64_t, 2> hashBytesTo128(std::span<const std::uint8_t> bytes,
                                                   std::uint64_t seed0,
                                                   std::uint64_t seed1) {
    std::uint64_t lo = 0x243f6a8885a308d3ULL ^ seed0;
    std::uint64_t hi = 0x13198a2e03707344ULL ^ seed1;

    lo = absorbBytes(lo, bytes, 0x100000001b3ULL);
    hi = absorbBytes(hi, bytes, 0xc3a5c85c97cb3127ULL);

    lo ^= static_cast<std::uint64_t>(bytes.size()) * 0x9e3779b97f4a7c15ULL;
    hi ^= static_cast<std::uint64_t>(bytes.size()) * 0xbf58476d1ce4e5b9ULL;

    lo = splitMix64(lo ^ hi);
    hi = splitMix64(hi ^ std::rotl(lo, 17));
    return {lo, hi};
}

inline GF128 hashBytesToField(std::span<const std::uint8_t> bytes,
                              std::uint64_t seed0,
                              std::uint64_t seed1) {
    const auto words = hashBytesTo128(bytes, seed0, seed1);
    return GF128(words[1], words[0]);
}

inline GF128 hashFieldToField(const GF128& value,
                              std::uint64_t seed0,
                              std::uint64_t seed1) {
    const auto bytes = value.toBytes();
    return hashBytesToField(std::span<const std::uint8_t>(bytes.data(), bytes.size()), seed0, seed1);
}

} // namespace okvs::internal
