// GF(2^128) field implementation used by the OKVS reproduction.

#pragma once

#include <array>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace okvs {

class GF128 {
public:
    constexpr GF128() : mWords{0, 0} {}
    constexpr GF128(uint64_t hi, uint64_t lo) : mWords{lo, hi} {}

    static GF128 fromUint128(__uint128_t value);
    static GF128 fromBytes(const std::array<std::uint8_t, 16>& bytes);

    static constexpr GF128 zero() { return {}; }
    static constexpr GF128 one() { return GF128{0, 1}; }

    [[nodiscard]] bool isZero() const { return mWords[0] == 0 && mWords[1] == 0; }

    [[nodiscard]] bool operator==(const GF128& rhs) const = default;

    GF128& operator^=(const GF128& rhs);
    GF128 operator^(const GF128& rhs) const;

    GF128& operator+=(const GF128& rhs) { return (*this ^= rhs); }
    GF128 operator+(const GF128& rhs) const { return (*this ^ rhs); }
    GF128& operator-=(const GF128& rhs) { return (*this ^= rhs); }
    GF128 operator-(const GF128& rhs) const { return (*this ^ rhs); }

    [[nodiscard]] GF128 square() const;

    GF128& operator*=(const GF128& rhs);
    GF128 operator*(const GF128& rhs) const;

    [[nodiscard]] GF128 inverse() const;

    [[nodiscard]] std::array<std::uint8_t, 16> toBytes() const;
    [[nodiscard]] std::string toHex() const;

    [[nodiscard]] uint64_t lo() const { return mWords[0]; }
    [[nodiscard]] uint64_t hi() const { return mWords[1]; }

private:
    std::array<uint64_t, 2> mWords;
};

std::ostream& operator<<(std::ostream& os, const GF128& value);

} // namespace okvs

