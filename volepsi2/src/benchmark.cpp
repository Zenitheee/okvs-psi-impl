#include "okvs/binned_encoder.h"
#include "okvs/encoder.h"
#include "okvs/psi.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Item = std::array<std::uint8_t, 16>;

enum class Mode {
    Okvs,
    Psi
};

struct Options {
    Mode mode = Mode::Okvs;
    std::size_t n = 1ull << 10;
    std::size_t trials = 1;
    std::size_t numThreads = 1;
    std::size_t binSizeHint = 1ull << 14;
    std::uint64_t seed = 0x123456789abcdef0ULL;
    bool verbose = false;
};

struct OkvsTrialResult {
    bool usedClustering = false;
    std::size_t tableSize = 0;
    std::size_t numBins = 1;
    std::size_t itemsPerBin = 0;
    std::size_t sparsePerBin = 0;
    std::size_t densePerBin = 0;
    double encodeMs = 0.0;
    double decodeMs = 0.0;
};

struct PsiTrialResult {
    bool usedClustering = false;
    bool usedRealVole = false;
    std::size_t okvsSize = 0;
    std::size_t intersectionSize = 0;
    double totalMs = 0.0;
};

[[nodiscard]] double elapsedMs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

[[nodiscard]] std::uint64_t parseU64(const char* name, const char* value) {
    try {
        const auto parsed = std::stoull(value);
        return static_cast<std::uint64_t>(parsed);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("Invalid numeric value for ") + name + ": " + value);
    }
}

[[nodiscard]] Options parseArgs(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error("Usage: volepsi2_bench <okvs|psi> [-n value|-nn value] [-t value] [-nt value] [-bs value] [-seed value] [-v]");
    }

    Options options;
    const std::string_view mode(argv[1]);
    if (mode == "okvs") {
        options.mode = Mode::Okvs;
    } else if (mode == "psi") {
        options.mode = Mode::Psi;
    } else {
        throw std::runtime_error("First argument must be 'okvs' or 'psi'.");
    }

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "-v") {
            options.verbose = true;
            continue;
        }

        if (i + 1 >= argc) {
            throw std::runtime_error("Missing value for argument: " + std::string(arg));
        }

        const auto value = argv[++i];
        if (arg == "-n") {
            options.n = static_cast<std::size_t>(parseU64("-n", value));
        } else if (arg == "-nn") {
            const auto nn = parseU64("-nn", value);
            if (nn >= std::numeric_limits<std::uint64_t>::digits) {
                throw std::runtime_error("-nn is too large.");
            }
            options.n = static_cast<std::size_t>(1ull << nn);
        } else if (arg == "-t") {
            options.trials = static_cast<std::size_t>(parseU64("-t", value));
        } else if (arg == "-nt") {
            options.numThreads = static_cast<std::size_t>(parseU64("-nt", value));
        } else if (arg == "-bs") {
            options.binSizeHint = static_cast<std::size_t>(parseU64("-bs", value));
        } else if (arg == "-seed") {
            options.seed = parseU64("-seed", value);
        } else {
            throw std::runtime_error("Unknown argument: " + std::string(arg));
        }
    }

    if (options.trials == 0) {
        throw std::runtime_error("-t must be positive.");
    }
    if (options.numThreads == 0) {
        throw std::runtime_error("-nt must be positive.");
    }

    return options;
}

void storeU64(std::uint8_t* dest, std::uint64_t value) {
    std::memcpy(dest, &value, sizeof(value));
}

[[nodiscard]] std::uint64_t splitMix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

[[nodiscard]] std::vector<Item> makeItems(std::size_t count,
                                          std::uint64_t seed,
                                          std::uint64_t domainTag) {
    std::vector<Item> items(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto& item = items[i];
        const auto hi = splitMix64(seed + 2 * static_cast<std::uint64_t>(i) + domainTag * 0x100000001b3ULL);
        const auto lo = (static_cast<std::uint64_t>(i) << 8) ^ domainTag;
        storeU64(item.data(), hi);
        storeU64(item.data() + sizeof(std::uint64_t), lo);
    }
    return items;
}

[[nodiscard]] std::vector<okvs::KeyView> makeViews(const std::vector<Item>& items) {
    std::vector<okvs::KeyView> views(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        views[i] = okvs::KeyView{items[i].data(), items[i].size()};
    }
    return views;
}

[[nodiscard]] std::vector<okvs::GF128> makeValues(std::size_t count, std::uint64_t seed) {
    std::vector<okvs::GF128> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto hi = splitMix64(seed + 2 * static_cast<std::uint64_t>(i));
        const auto lo = splitMix64(seed + 2 * static_cast<std::uint64_t>(i) + 1);
        values.emplace_back(hi, lo);
    }
    return values;
}

void verifyDecode(std::span<const okvs::GF128> decoded,
                  std::span<const okvs::GF128> expected) {
    if (decoded.size() != expected.size()) {
        throw std::runtime_error("Decoded vector size mismatch.");
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (!(decoded[i] == expected[i])) {
            throw std::runtime_error("Decode verification failed.");
        }
    }
}

[[nodiscard]] OkvsTrialResult runOkvsTrial(const Options& options,
                                           std::span<const okvs::KeyView> keys,
                                           std::span<const okvs::GF128> values,
                                           std::uint64_t trialSeed) {
    OkvsTrialResult result;
    result.usedClustering = options.binSizeHint != 0 && keys.size() > options.binSizeHint;

    okvs::OkvsConfig config;

    if (result.usedClustering) {
        const auto encodeStart = Clock::now();
        okvs::BinnedOkvsEncoder encoder(config, trialSeed, keys.size(), options.binSizeHint);
        std::vector<okvs::GF128> table(encoder.totalSize(), okvs::GF128::zero());
        encoder.encode(keys, values, std::span<okvs::GF128>(table.data(), table.size()), options.numThreads);
        const auto encodeEnd = Clock::now();

        std::vector<okvs::GF128> decoded(keys.size(), okvs::GF128::zero());
        const auto decodeStart = Clock::now();
        encoder.decode(keys, std::span<okvs::GF128>(decoded.data(), decoded.size()),
                       std::span<const okvs::GF128>(table.data(), table.size()), options.numThreads);
        const auto decodeEnd = Clock::now();

        verifyDecode(std::span<const okvs::GF128>(decoded.data(), decoded.size()), values);

        result.tableSize = encoder.totalSize();
        result.numBins = encoder.numBins();
        result.itemsPerBin = encoder.itemsPerBin();
        result.sparsePerBin = encoder.sparsePerBin();
        result.densePerBin = encoder.densePerBin();
        result.encodeMs = elapsedMs(encodeStart, encodeEnd);
        result.decodeMs = elapsedMs(decodeStart, decodeEnd);
        return result;
    }

    const auto encodeStart = Clock::now();
    okvs::OkvsEncoder encoder(config, trialSeed);
    auto table = encoder.encode(keys, values);
    const auto encodeEnd = Clock::now();

    okvs::OkvsEncoder::EncodedTableView view{
        std::span<const okvs::GF128>(table.data.data(), table.data.size()),
        table.sparseColumns,
        table.denseColumns
    };
    std::vector<okvs::GF128> decoded(keys.size(), okvs::GF128::zero());
    const auto decodeStart = Clock::now();
    encoder.decode(keys, std::span<okvs::GF128>(decoded.data(), decoded.size()), view, options.numThreads);
    const auto decodeEnd = Clock::now();

    verifyDecode(std::span<const okvs::GF128>(decoded.data(), decoded.size()), values);

    result.tableSize = table.data.size();
    result.numBins = 1;
    result.itemsPerBin = keys.size();
    result.sparsePerBin = table.sparseColumns;
    result.densePerBin = table.denseColumns;
    result.encodeMs = elapsedMs(encodeStart, encodeEnd);
    result.decodeMs = elapsedMs(decodeStart, decodeEnd);
    return result;
}

[[nodiscard]] PsiTrialResult runPsiTrial(const Options& options,
                                         std::span<const okvs::KeyView> receiver,
                                         std::span<const okvs::KeyView> sender,
                                         std::uint64_t trialSeed) {
    okvs::PsiConfig config;
    config.binSizeHint = options.binSizeHint;
    config.numThreads = options.numThreads;
    config.deterministicSeedEnabled = true;
    config.seed = trialSeed;

    okvs::SemiHonestPsi psi(config);
    const auto start = Clock::now();
    const auto result = psi.run(receiver, sender);
    const auto end = Clock::now();

    if (!result.intersectionIndices.empty()) {
        throw std::runtime_error("Expected disjoint benchmark sets.");
    }

    return PsiTrialResult{
        result.usedClustering,
        result.usedRealVole,
        result.okvsSize,
        result.intersectionIndices.size(),
        elapsedMs(start, end)
    };
}

void printOkvsSummary(const Options& options,
                      const std::vector<OkvsTrialResult>& trials) {
    double encodeSum = 0.0;
    double decodeSum = 0.0;
    for (const auto& trial : trials) {
        encodeSum += trial.encodeMs;
        decodeSum += trial.decodeMs;
    }

    const auto& last = trials.back();
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "mode=okvs"
              << " n=" << options.n
              << " trials=" << options.trials
              << " threads=" << options.numThreads
              << " bin_size_hint=" << options.binSizeHint
              << " clustered=" << (last.usedClustering ? 1 : 0)
              << " table_size=" << last.tableSize
              << " bins=" << last.numBins
              << " items_per_bin=" << last.itemsPerBin
              << " sparse_per_bin=" << last.sparsePerBin
              << " dense_per_bin=" << last.densePerBin
              << " encode_ms_avg=" << (encodeSum / trials.size())
              << " decode_ms_avg=" << (decodeSum / trials.size())
              << " total_ms_avg=" << ((encodeSum + decodeSum) / trials.size())
              << '\n';
}

void printPsiSummary(const Options& options,
                     const std::vector<PsiTrialResult>& trials) {
    double totalSum = 0.0;
    for (const auto& trial : trials) {
        totalSum += trial.totalMs;
    }

    const auto& last = trials.back();
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "mode=psi"
              << " n=" << options.n
              << " trials=" << options.trials
              << " threads=" << options.numThreads
              << " bin_size_hint=" << options.binSizeHint
              << " clustered=" << (last.usedClustering ? 1 : 0)
              << " real_vole=" << (last.usedRealVole ? 1 : 0)
              << " okvs_size=" << last.okvsSize
              << " intersection=" << last.intersectionSize
              << " total_ms_avg=" << (totalSum / trials.size())
              << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parseArgs(argc, argv);

        if (options.mode == Mode::Okvs) {
            const auto items = makeItems(options.n, options.seed, 0x01ULL);
            const auto views = makeViews(items);
            const auto values = makeValues(options.n, options.seed ^ 0x9e3779b97f4a7c15ULL);

            std::vector<OkvsTrialResult> trials;
            trials.reserve(options.trials);
            for (std::size_t i = 0; i < options.trials; ++i) {
                const auto trialSeed = options.seed + static_cast<std::uint64_t>(i) * 0x100000001b3ULL;
                const auto result = runOkvsTrial(
                    options,
                    std::span<const okvs::KeyView>(views.data(), views.size()),
                    std::span<const okvs::GF128>(values.data(), values.size()),
                    trialSeed);
                if (options.verbose) {
                    std::cout << "trial=" << i
                              << " encode_ms=" << std::fixed << std::setprecision(3) << result.encodeMs
                              << " decode_ms=" << result.decodeMs
                              << '\n';
                }
                trials.push_back(result);
            }
            printOkvsSummary(options, trials);
            return 0;
        }

        const auto receiverItems = makeItems(options.n, options.seed, 0x11ULL);
        const auto senderItems = makeItems(options.n, options.seed ^ 0xa5a5a5a5a5a5a5a5ULL, 0x22ULL);
        const auto receiverViews = makeViews(receiverItems);
        const auto senderViews = makeViews(senderItems);

        std::vector<PsiTrialResult> trials;
        trials.reserve(options.trials);
        for (std::size_t i = 0; i < options.trials; ++i) {
            const auto trialSeed = options.seed + static_cast<std::uint64_t>(i) * 0x9e3779b97f4a7c15ULL;
            const auto result = runPsiTrial(
                options,
                std::span<const okvs::KeyView>(receiverViews.data(), receiverViews.size()),
                std::span<const okvs::KeyView>(senderViews.data(), senderViews.size()),
                trialSeed);
            if (options.verbose) {
                std::cout << "trial=" << i
                          << " total_ms=" << std::fixed << std::setprecision(3) << result.totalMs
                          << " okvs_size=" << result.okvsSize
                          << '\n';
            }
            trials.push_back(result);
        }
        printPsiSummary(options, trials);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
