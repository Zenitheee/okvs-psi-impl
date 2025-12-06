#include "okvs/encoder.h"

#include <iostream>
#include <random>
#include <string>
#include <vector>

int main() {
    using namespace okvs;

    OkvsConfig config;
    OkvsEncoder encoder(config, 0x5eed5eedULL);

    std::mt19937_64 rng(1337);
    // keep the test sizes large enough so that sparse columns exceed the row weight.
    std::vector<std::size_t> sizes = {8, 64, 256};

    for (auto n : sizes) {
        std::vector<std::string> keys;
        std::vector<GF128> values;
        keys.reserve(n);
        values.reserve(n);

        for (std::size_t i = 0; i < n; ++i) {
            keys.emplace_back("key-" + std::to_string(i));
            uint64_t lo = rng();
            uint64_t hi = rng();
            values.emplace_back(GF128(hi, lo));
        }

        okvs::OkvsEncoder::EncodedTable table;
        try {
            table = encoder.encode(keys, values);
        } catch (const std::exception& ex) {
            std::cerr << "encode failed for n=" << n << " with error: " << ex.what() << '\n';
            return 1;
        }
        for (std::size_t i = 0; i < n; ++i) {
            auto recovered = encoder.decode(keys[i], table);
            if (!(recovered == values[i])) {
                std::cerr << "Mismatch at set size " << n << " index " << i << '\n';
                return 1;
            }
        }
    }

    std::cout << "OKVS round-trip tests passed.\n";
    return 0;
}


