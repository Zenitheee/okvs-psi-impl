#include "okvs/gf128.h"
#include <iostream>
#include <random>
#include <vector>
#include <cassert>

int main() {
    using namespace okvs;

    // Test 0
    GF128 zero = GF128::zero();
    if (!(zero.square() == (zero * zero))) {
        std::cerr << "Test failed for 0\n";
        return 1;
    }
    if (!(zero.square() == zero)) {
        std::cerr << "Square of 0 is not 0\n";
        return 1;
    }

    // Test 1
    GF128 one = GF128::one();
    if (!(one.square() == (one * one))) {
        std::cerr << "Test failed for 1\n";
        return 1;
    }
    if (!(one.square() == one)) {
        std::cerr << "Square of 1 is not 1\n";
        return 1;
    }
    if (!(one.inverse() == one)) {
        std::cerr << "Inverse of 1 is not 1\n";
        return 1;
    }

    // Random tests
    std::mt19937_64 rng(12345);
    for (int i = 0; i < 10000; ++i) {
        uint64_t lo = rng();
        uint64_t hi = rng();
        if (lo == 0 && hi == 0) continue; // Skip zero for inverse

        GF128 a(hi, lo);
        
        // Square test
        GF128 sq_op = a * a;
        GF128 sq_opt = a.square();

        if (!(sq_op == sq_opt)) {
            std::cerr << "Square test failed for " << a << "\n";
            std::cerr << "Expected: " << sq_op << "\n";
            std::cerr << "Got:      " << sq_opt << "\n";
            return 1;
        }

        // Inverse test
        GF128 inv = a.inverse();
        GF128 prod = a * inv;
        
        if (!(prod == GF128::one())) {
            std::cerr << "Inverse test failed for " << a << "\n";
            std::cerr << "Inverse: " << inv << "\n";
            std::cerr << "Product: " << prod << "\n";
            return 1;
        }
    }

    std::cout << "GF128 square and inverse tests passed!\n";
    return 0;
}
