#include "okvs/gf128.h"

#include <algorithm>
#include <bit>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <wmmintrin.h>
#include <emmintrin.h>
#include <smmintrin.h> // For _mm_extract_epi64

namespace okvs {

GF128 GF128::fromUint128(__uint128_t value) {
    GF128 out;
    out.mWords[0] = static_cast<uint64_t>(value);
    out.mWords[1] = static_cast<uint64_t>(value >> 64);
    return out;
}

GF128 GF128::fromBytes(const std::array<std::uint8_t, 16>& bytes) {
    GF128 result;
    uint64_t lo = 0;
    uint64_t hi = 0;
    for (int i = 0; i < 8; ++i) {
        lo |= static_cast<uint64_t>(bytes[i]) << (8 * i);
        hi |= static_cast<uint64_t>(bytes[8 + i]) << (8 * i);
    }
    result.mWords[0] = lo;
    result.mWords[1] = hi;
    return result;
}

GF128& GF128::operator^=(const GF128& rhs) {
    mWords[0] ^= rhs.mWords[0];
    mWords[1] ^= rhs.mWords[1];
    return *this;
}

GF128 GF128::operator^(const GF128& rhs) const {
    GF128 tmp(*this);
    tmp ^= rhs;
    return tmp;
}

GF128& GF128::operator*=(const GF128& rhs) {
    *this = *this * rhs;
    return *this;
}

GF128 GF128::operator*(const GF128& rhs) const {
    __m128i a = _mm_set_epi64x(mWords[1], mWords[0]);
    __m128i b = _mm_set_epi64x(rhs.mWords[1], rhs.mWords[0]);

    // Polynomial multiplication: a * b
    __m128i tmp0 = _mm_clmulepi64_si128(a, b, 0x00);
    __m128i tmp1 = _mm_clmulepi64_si128(a, b, 0x10);
    __m128i tmp2 = _mm_clmulepi64_si128(a, b, 0x01);
    __m128i tmp3 = _mm_clmulepi64_si128(a, b, 0x11);

    __m128i t1 = _mm_xor_si128(tmp1, tmp2);
    __m128i C_lo = _mm_xor_si128(tmp0, _mm_slli_si128(t1, 8));
    __m128i C_hi = _mm_xor_si128(tmp3, _mm_srli_si128(t1, 8));

    // Reduction mod x^128 + x^7 + x^2 + x + 1 (0x87)
    __m128i P = _mm_set_epi64x(0, 0x87);

    // H * P
    __m128i H_mul_P_lo = _mm_clmulepi64_si128(C_hi, P, 0x00);
    __m128i H_mul_P_hi = _mm_clmulepi64_si128(C_hi, P, 0x01);

    __m128i T_lo = _mm_xor_si128(H_mul_P_lo, _mm_slli_si128(H_mul_P_hi, 8));
    __m128i T_hi = _mm_srli_si128(H_mul_P_hi, 8); // This contains the overflow bits from H*P

    // Reduce again
    __m128i T_hi_mul_P = _mm_clmulepi64_si128(T_hi, P, 0x00);

    __m128i res = _mm_xor_si128(C_lo, T_lo);
    res = _mm_xor_si128(res, T_hi_mul_P);

    GF128 out;
    out.mWords[0] = _mm_cvtsi128_si64(res);
    out.mWords[1] = _mm_extract_epi64(res, 1);
    return out;
}

GF128 GF128::inverse() const {
    if (isZero()) {
        throw std::runtime_error("GF128 inverse of zero is undefined");
    }

    // Inversion using Fermat's Little Theorem: a^(2^128 - 2)
    // 2^128 - 2 = 11...110 (127 ones followed by a 0)
    
    GF128 res = *this;
    // Compute res = base^(2^127 - 1)
    for (int i = 0; i < 126; ++i) {
        res = res * res;
        res = res * (*this);
    }
    // Final square to shift left (make it ...110)
    res = res * res;

    return res;
}

std::array<std::uint8_t, 16> GF128::toBytes() const {
    std::array<std::uint8_t, 16> bytes{};
    for (int i = 0; i < 8; ++i) {
        bytes[i] = static_cast<std::uint8_t>((mWords[0] >> (8 * i)) & 0xFF);
        bytes[8 + i] = static_cast<std::uint8_t>((mWords[1] >> (8 * i)) & 0xFF);
    }
    return bytes;
}

std::string GF128::toHex() const {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    auto bytes = toBytes();
    for (int i = 15; i >= 0; --i) {
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return oss.str();
}

std::ostream& operator<<(std::ostream& os, const GF128& value) {
    return os << "0x" << value.toHex();
}

} // namespace okvs

