#include "okvs/encoder.h"

#include <iostream>
#include <random>
#include <span>
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

        std::vector<KeyView> keyViews(keys.size());
        for (std::size_t i = 0; i < keys.size(); ++i) {
            const auto& key = keys[i];
            const auto* data = key.empty() ? nullptr : key.data();
            keyViews[i] = KeyView{
                reinterpret_cast<const std::uint8_t*>(data),
                key.size()
            };
        }

        okvs::OkvsEncoder::EncodedTable table;
        try {
            table = encoder.encode(
                std::span<const KeyView>(keyViews.data(), keyViews.size()),
                std::span<const GF128>(values.data(), values.size()));
        } catch (const std::exception& ex) {
            std::cerr << "encode failed for n=" << n << " with error: " << ex.what() << '\n';
            return 1;
        }

        OkvsEncoder::EncodedTableView tableView{
            std::span<const GF128>(table.data),
            table.sparseColumns,
            table.denseColumns
        };

        for (std::size_t i = 0; i < n; ++i) {
            auto recovered = encoder.decode(keyViews[i], tableView);
            if (!(recovered == values[i])) {
                std::cerr << "Mismatch at set size " << n << " index " << i << '\n';
                return 1;
            }
        }
    }

    std::cout << "OKVS round-trip tests passed.\n";
    return 0;
}


