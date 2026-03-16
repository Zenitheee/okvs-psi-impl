#include "okvs/binned_encoder.h"

#include <cstdint>
#include <iostream>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using okvs::BinnedOkvsEncoder;
using okvs::GF128;
using okvs::KeyView;
using okvs::OkvsConfig;

std::vector<KeyView> makeKeyViews(const std::vector<std::string>& keys) {
    std::vector<KeyView> views(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto* data = keys[i].empty() ? nullptr : keys[i].data();
        views[i] = KeyView{
            reinterpret_cast<const std::uint8_t*>(data),
            keys[i].size()
        };
    }
    return views;
}

std::vector<GF128> makeValues(std::size_t count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<GF128> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        values.emplace_back(rng(), rng());
    }
    return values;
}

template <typename Fn>
bool expectRuntimeError(const char* label, Fn&& fn) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        return true;
    } catch (const std::exception& ex) {
        std::cerr << label << " threw an unexpected exception: " << ex.what() << '\n';
        return false;
    }

    std::cerr << label << " did not throw\n";
    return false;
}

bool verifyDecoded(const char* label,
                   std::span<const GF128> decoded,
                   std::span<const GF128> expected) {
    if (decoded.size() != expected.size()) {
        std::cerr << label << " size mismatch\n";
        return false;
    }

    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (!(decoded[i] == expected[i])) {
            std::cerr << label << " mismatch at index " << i << '\n';
            return false;
        }
    }

    return true;
}

bool runRoundTripCase() {
    OkvsConfig config;
    constexpr std::size_t n = 96;
    constexpr std::size_t binSizeHint = 8;
    BinnedOkvsEncoder encoder(config, 0x42424242ULL, n, binSizeHint);

    if (encoder.numBins() <= 1) {
        std::cerr << "binned encoder should use more than one bin for the test case\n";
        return false;
    }

    std::vector<std::string> keys;
    keys.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == 0) {
            keys.emplace_back();
        } else {
            keys.emplace_back("binned-roundtrip-key-" + std::to_string(i));
        }
    }

    const auto values = makeValues(n, 0x51515151ULL);
    const auto keyViews = makeKeyViews(keys);
    std::vector<GF128> table(encoder.totalSize(), GF128::zero());

    encoder.encode(
        std::span<const KeyView>(keyViews.data(), keyViews.size()),
        std::span<const GF128>(values.data(), values.size()),
        std::span<GF128>(table.data(), table.size()),
        4);

    std::vector<GF128> singleThread(values.size(), GF128::zero());
    std::vector<GF128> multiThread(values.size(), GF128::zero());
    encoder.decode(
        std::span<const KeyView>(keyViews.data(), keyViews.size()),
        std::span<GF128>(singleThread.data(), singleThread.size()),
        std::span<const GF128>(table.data(), table.size()),
        1);
    encoder.decode(
        std::span<const KeyView>(keyViews.data(), keyViews.size()),
        std::span<GF128>(multiThread.data(), multiThread.size()),
        std::span<const GF128>(table.data(), table.size()),
        4);

    if (!verifyDecoded("binned single-thread decode", singleThread, values)) {
        return false;
    }
    if (!verifyDecoded("binned multi-thread decode", multiThread, values)) {
        return false;
    }

    for (std::size_t i = 0; i < n; ++i) {
        const auto recovered = encoder.decode(keyViews[i], std::span<const GF128>(table.data(), table.size()));
        if (!(recovered == values[i])) {
            std::cerr << "binned single-key decode mismatch at index " << i << '\n';
            return false;
        }
    }

    return true;
}

bool runValidationCases() {
    OkvsConfig config;
    BinnedOkvsEncoder encoder(config, 0x99999999ULL, 12, 3);

    const std::vector<std::string> validKeys = {"bin-valid-key"};
    const auto validViews = makeKeyViews(validKeys);
    const std::vector<GF128> values = {GF128::one()};

    std::vector<GF128> wrongOutput(encoder.totalSize() - 1, GF128::zero());
    if (!expectRuntimeError("binned wrong output size", [&] {
            encoder.encode(
                std::span<const KeyView>(validViews.data(), validViews.size()),
                std::span<const GF128>(values.data(), values.size()),
                std::span<GF128>(wrongOutput.data(), wrongOutput.size()),
                1);
        })) {
        return false;
    }

    const std::vector<KeyView> invalidKeys = {
        KeyView{nullptr, 5}
    };
    std::vector<GF128> table(encoder.totalSize(), GF128::zero());
    if (!expectRuntimeError("binned null key encode", [&] {
            encoder.encode(
                std::span<const KeyView>(invalidKeys.data(), invalidKeys.size()),
                std::span<const GF128>(values.data(), values.size()),
                std::span<GF128>(table.data(), table.size()),
                1);
        })) {
        return false;
    }

    if (!expectRuntimeError("binned wrong table size", [&] {
            const auto ignored = encoder.decode(
                validViews[0],
                std::span<const GF128>(wrongOutput.data(), wrongOutput.size()));
            (void)ignored;
        })) {
        return false;
    }

    return true;
}

} // namespace

int main() {
    if (!runRoundTripCase()) {
        return 1;
    }
    if (!runValidationCases()) {
        return 1;
    }

    std::cout << "Binned OKVS correctness tests passed.\n";
    return 0;
}
