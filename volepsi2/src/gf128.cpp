#include "okvs/gf128.h"

#include <algorithm>
#include <bit>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace okvs {

namespace {

struct Poly256 {
    std::array<uint64_t, 4> words{};

    Poly256() = default;
    explicit Poly256(uint64_t value) { words[0] = value; }

    static Poly256 fromGF128(const GF128& gf) {
        Poly256 p;
        p.words[0] = gf.lo();
        p.words[1] = gf.hi();
        return p;
    }

    static Poly256 modulus() {
        Poly256 m;
        m.words[0] = 0x87; // x^7 + x^2 + x + 1
        m.words[2] = 0x1;   // x^128
        return m;
    }

    [[nodiscard]] bool isZero() const {
        return std::all_of(words.begin(), words.end(), [](uint64_t w) { return w == 0; });
    }

    [[nodiscard]] bool isOne() const {
        return words[0] == 1 && words[1] == 0 && words[2] == 0 && words[3] == 0;
    }

    [[nodiscard]] int degree() const {
        for (int i = 3; i >= 0; --i) {
            if (words[i] != 0) {
                return i * 64 + (63 - std::countl_zero(words[i]));
            }
        }
        return -1;
    }

    [[nodiscard]] bool getBit(int idx) const {
        int word = idx / 64;
        int bit = idx % 64;
        if (word < 0 || word >= static_cast<int>(words.size())) {
            return false;
        }
        return (words[word] >> bit) & 1ULL;
    }

    [[nodiscard]] Poly256 shiftedLeft(int shift) const {
        if (shift <= 0) {
            return *this;
        }

        Poly256 result;
        if (shift >= 256) {
            return result;
        }

        int wordShift = shift / 64;
        int bitShift = shift % 64;

        for (int i = 3; i >= 0; --i) {
            int sourceIndex = i - wordShift;
            if (sourceIndex < 0) {
                continue;
            }

            uint64_t value = words[sourceIndex] << bitShift;
            result.words[i] |= value;

            if (bitShift != 0 && sourceIndex - 1 >= 0) {
                uint64_t carry = words[sourceIndex - 1] >> (64 - bitShift);
            result.words[i] |= carry;
            }
        }

        return result;
    }

    Poly256& operator^=(const Poly256& rhs) {
        for (size_t i = 0; i < words.size(); ++i) {
            words[i] ^= rhs.words[i];
        }
        return *this;
    }

    [[nodiscard]] Poly256 operator^(const Poly256& rhs) const {
        Poly256 tmp(*this);
        tmp ^= rhs;
        return tmp;
    }
};

Poly256 reduce(const Poly256& value) {
    Poly256 result = value;
    Poly256 mod = Poly256::modulus();
    int deg = result.degree();
    while (deg >= 128) {
        int shift = deg - 128;
        result ^= mod.shiftedLeft(shift);
        deg = result.degree();
    }
    return result;
}

GF128 fromPoly(const Poly256& value) {
    Poly256 reduced = reduce(value);
    return GF128(reduced.words[1], reduced.words[0]);
}

} // namespace

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
    Poly256 a = Poly256::fromGF128(*this);
    Poly256 accum;

    const std::array<uint64_t, 2> rhsWords = {rhs.lo(), rhs.hi()};
    for (int word = 0; word < 2; ++word) {
        uint64_t current = rhsWords[word];
        for (int bit = 0; bit < 64; ++bit) {
            if ((current >> bit) & 1ULL) {
                accum ^= a.shiftedLeft(word * 64 + bit);
            }
        }
    }

    return fromPoly(accum);
}

GF128 GF128::inverse() const {
    if (isZero()) {
        throw std::runtime_error("GF128 inverse of zero is undefined");
    }

    Poly256 u = Poly256::fromGF128(*this);
    Poly256 v = Poly256::modulus();
    Poly256 g1(1);
    Poly256 g2;

    while (!u.isOne()) {
        int du = u.degree();
        int dv = v.degree();

        if (du < dv) {
            std::swap(u, v);
            std::swap(g1, g2);
            std::swap(du, dv);
        }

        int shift = du - dv;
        u ^= v.shiftedLeft(shift);
        g1 ^= g2.shiftedLeft(shift);
    }

    return fromPoly(g1);
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

