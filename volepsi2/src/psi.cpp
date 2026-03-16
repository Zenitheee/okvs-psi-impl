#include "okvs/psi.h"

#include "hash_utils.h"
#include "okvs/binned_encoder.h"

#if OKVS_ENABLE_REAL_VOLE
#include "coproto/Socket/LocalAsyncSock.h"
#include "libOTe/Vole/Silent/SilentVoleReceiver.h"
#include "libOTe/Vole/Silent/SilentVoleSender.h"
#include "macoro/sync_wait.h"
#include "macoro/when_all.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace okvs {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kHBSeed0 = 0x5b1f3d8a6c2e9071ULL;
constexpr std::uint64_t kHBSeed1 = 0x94d049bb133111ebULL;
constexpr std::uint64_t kHOSeed0 = 0xbf58476d1ce4e5b9ULL;
constexpr std::uint64_t kHOSeed1 = 0x9e3779b97f4a7c15ULL;

double elapsedMs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

std::size_t bytesForFieldElements(std::size_t count) {
    return count * sizeof(GF128);
}

std::vector<std::uint8_t> serializeFieldVector(std::span<const GF128> values) {
    std::vector<std::uint8_t> bytes(values.size() * 16, 0);
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto fieldBytes = values[i].toBytes();
        std::memcpy(bytes.data() + i * 16, fieldBytes.data(), fieldBytes.size());
    }
    return bytes;
}

std::vector<GF128> deserializeFieldVector(std::span<const std::uint8_t> bytes) {
    if (bytes.size() % 16 != 0) {
        throw std::runtime_error("field vector payload size is not a multiple of 16 bytes");
    }

    const auto count = bytes.size() / 16;
    std::vector<GF128> values(count, GF128::zero());
    for (std::size_t i = 0; i < count; ++i) {
        std::array<std::uint8_t, 16> fieldBytes{};
        std::memcpy(fieldBytes.data(), bytes.data() + i * 16, fieldBytes.size());
        values[i] = GF128::fromBytes(fieldBytes);
    }
    return values;
}

std::size_t sendFieldVector(coproto::Socket& socket, std::span<const GF128> values) {
    auto bytes = serializeFieldVector(values);
    const auto byteCount = bytes.size();
    macoro::sync_wait(socket.send(std::move(bytes)));
    macoro::sync_wait(socket.flush());
    return byteCount;
}

std::vector<GF128> recvFieldVector(coproto::Socket& socket, std::size_t count) {
    std::vector<std::uint8_t> bytes(count * 16, 0);
    macoro::sync_wait(socket.recv(bytes));
    return deserializeFieldVector(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

void rethrowIfSet(const std::exception_ptr& error) {
    if (error) {
        std::rethrow_exception(error);
    }
}

std::string exceptionMessage(const std::exception_ptr& error) {
    if (!error) {
        return {};
    }

    try {
        std::rethrow_exception(error);
    } catch (const std::exception& ex) {
        return ex.what();
    } catch (...) {
        return "unknown exception";
    }
}

std::span<const std::uint8_t> asBytes(KeyView key) {
    if (key.size == 0 || key.data == nullptr) {
        return {};
    }
    return std::span<const std::uint8_t>(key.data, key.size);
}

class SessionRng {
public:
    explicit SessionRng(std::uint64_t seed)
        : mState(seed) {}

    std::uint64_t nextU64() {
        mState += 0x9e3779b97f4a7c15ULL;
        return internal::splitMix64(mState);
    }

    GF128 nextField() {
        return GF128(nextU64(), nextU64());
    }

private:
    std::uint64_t mState;
};

struct VoleCorrelation {
    GF128 delta = GF128::zero();
    std::vector<GF128> senderB;
    std::vector<GF128> receiverA;
    std::vector<GF128> receiverC;
    std::size_t transportBytes = 0;
    bool usedRealBackend = false;
};

struct GF128Hash {
    std::size_t operator()(const GF128& value) const {
        const auto mixed = internal::splitMix64(value.lo() ^ std::rotl(value.hi(), 23));
        return static_cast<std::size_t>(mixed);
    }
};

GF128 hashToBaseField(KeyView key, const GF128& salt) {
    return internal::hashBytesToField(
        asBytes(key),
        salt.lo() ^ kHBSeed0,
        salt.hi() ^ kHBSeed1);
}

GF128 hashOutput(const GF128& value) {
    return internal::hashFieldToField(value, kHOSeed0, kHOSeed1);
}

#if OKVS_ENABLE_REAL_VOLE
oc::block toBlock(const GF128& value) {
    return oc::block(value.hi(), value.lo());
}

GF128 fromBlock(const oc::block& value) {
    return GF128(value.get<std::uint64_t>(1), value.get<std::uint64_t>(0));
}

oc::block nextBlockSeed(SessionRng& rng) {
    return toBlock(rng.nextField());
}

VoleCorrelation generateRealVole(std::size_t correlationSize,
                                 std::size_t numThreads,
                                 SessionRng& rng) {
    VoleCorrelation correlation;
    correlation.delta = rng.nextField();
    correlation.usedRealBackend = true;

    if (correlationSize == 0) {
        return correlation;
    }

    oc::SilentVoleSender<oc::block, oc::block, oc::CoeffCtxGF128> sender;
    oc::SilentVoleReceiver<oc::block, oc::block, oc::CoeffCtxGF128> receiver;
    const auto threadCount = static_cast<oc::u64>(std::max<std::size_t>(1, numThreads));
    receiver.mNumThreads = threadCount;

    auto sockets = coproto::LocalAsyncSocket::makePair();
    oc::PRNG senderPrng(nextBlockSeed(rng));
    oc::PRNG receiverPrng(nextBlockSeed(rng));

    auto senderTask = sender.silentSendInplace(
        toBlock(correlation.delta),
        static_cast<oc::u64>(correlationSize),
        senderPrng,
        sockets[0]);
    auto receiverTask = receiver.silentReceiveInplace(
        static_cast<oc::u64>(correlationSize),
        receiverPrng,
        sockets[1]);

    auto joined = macoro::sync_wait(
        macoro::when_all_ready(std::move(senderTask), std::move(receiverTask)));
    std::get<0>(joined).result();
    std::get<1>(joined).result();
    correlation.transportBytes = sockets[0].bytesSent() + sockets[1].bytesSent();

    if (sender.mB.size() != correlationSize ||
        receiver.mA.size() != correlationSize ||
        receiver.mC.size() != correlationSize) {
        throw std::runtime_error("silent VOLE returned an unexpected number of correlations");
    }

    correlation.senderB.resize(correlationSize, GF128::zero());
    correlation.receiverA.resize(correlationSize, GF128::zero());
    correlation.receiverC.resize(correlationSize, GF128::zero());
    for (std::size_t i = 0; i < correlationSize; ++i) {
        correlation.senderB[i] = fromBlock(sender.mB[i]);
        correlation.receiverA[i] = fromBlock(receiver.mA[i]);
        correlation.receiverC[i] = fromBlock(receiver.mC[i]);
    }

    return correlation;
}
#endif

[[maybe_unused]] VoleCorrelation generateSimulatedVole(std::size_t correlationSize, SessionRng& rng) {
    VoleCorrelation correlation;
    correlation.delta = rng.nextField();
    correlation.senderB.resize(correlationSize, GF128::zero());
    correlation.receiverA.resize(correlationSize, GF128::zero());
    correlation.receiverC.resize(correlationSize, GF128::zero());

    for (std::size_t i = 0; i < correlationSize; ++i) {
        const auto simulatedA = rng.nextField();
        const auto simulatedB = rng.nextField();
        correlation.receiverC[i] = simulatedA;
        correlation.senderB[i] = simulatedB;
        correlation.receiverA[i] = simulatedA * correlation.delta + simulatedB;
    }

    return correlation;
}

VoleCorrelation generateVoleCorrelation(std::size_t correlationSize,
                                        std::size_t numThreads,
                                        SessionRng& rng) {
#if OKVS_ENABLE_REAL_VOLE
    return generateRealVole(correlationSize, numThreads, rng);
#else
    (void)numThreads;
    return generateSimulatedVole(correlationSize, rng);
#endif
}

class ProtocolOkvs {
public:
    ProtocolOkvs(const PsiConfig& config, std::size_t receiverSize, std::uint64_t seed)
        : mUseClustering(config.binSizeHint != 0 && receiverSize > config.binSizeHint),
          mNumThreads(std::max<std::size_t>(1, config.numThreads)) {
        if (mUseClustering) {
            mBinned = std::make_unique<BinnedOkvsEncoder>(
                config.okvs,
                seed,
                receiverSize,
                config.binSizeHint);
        } else {
            mEncoder = std::make_unique<OkvsEncoder>(config.okvs, seed);
            mSparseColumns = mEncoder->sparseSize(receiverSize);
            mDenseColumns = mEncoder->denseSize(receiverSize);
        }
    }

    [[nodiscard]] std::size_t tableSize() const {
        if (mUseClustering) {
            return mBinned->totalSize();
        }
        return mSparseColumns + mDenseColumns;
    }

    [[nodiscard]] bool usesClustering() const {
        return mUseClustering;
    }

    [[nodiscard]] std::vector<GF128> encode(std::span<const KeyView> keys,
                                            std::span<const GF128> values) const {
        if (mUseClustering) {
            std::vector<GF128> table(mBinned->totalSize(), GF128::zero());
            mBinned->encode(keys, values, std::span<GF128>(table.data(), table.size()), mNumThreads);
            return table;
        }

        auto table = mEncoder->encode(keys, values);
        return table.data;
    }

    void decode(std::span<const KeyView> keys,
                std::span<GF128> values,
                std::span<const GF128> table) const {
        if (mUseClustering) {
            mBinned->decode(keys, values, table, mNumThreads);
            return;
        }

        OkvsEncoder::EncodedTableView view{table, mSparseColumns, mDenseColumns};
        mEncoder->decode(keys, values, view, mNumThreads);
    }

private:
    bool mUseClustering = false;
    std::size_t mNumThreads = 1;
    std::size_t mSparseColumns = 0;
    std::size_t mDenseColumns = 0;
    std::unique_ptr<OkvsEncoder> mEncoder;
    std::unique_ptr<BinnedOkvsEncoder> mBinned;
};

void publishStage(PsiResult& result,
                  Clock::time_point protocolStart,
                  PsiStageStat stage,
                  const PsiTelemetryCallback& onUpdate) {
    result.telemetry.totalNetworkBytes += stage.networkBytes;
    result.telemetry.stages.push_back(std::move(stage));
    result.telemetry.totalDurationMs = elapsedMs(protocolStart, Clock::now());
    result.telemetry.okvsSize = result.okvsSize;
    result.telemetry.intersectionSize = result.intersectionIndices.size();
    result.telemetry.usedClustering = result.usedClustering;
    result.telemetry.usedRealVole = result.usedRealVole;
    if (onUpdate) {
        onUpdate(result.telemetry);
    }
}

} // namespace

SemiHonestPsi::SemiHonestPsi(PsiConfig config)
    : mConfig(std::move(config)) {}

PsiResult SemiHonestPsi::run(std::span<const KeyView> receiverSet,
                             std::span<const KeyView> senderSet) const {
    return run(receiverSet, senderSet, {});
}

PsiResult SemiHonestPsi::run(std::span<const KeyView> receiverSet,
                             std::span<const KeyView> senderSet,
                             const PsiTelemetryCallback& onUpdate) const {
    PsiResult result;
    result.telemetry.receiverSetSize = receiverSet.size();
    result.telemetry.senderSetSize = senderSet.size();
    const auto protocolStart = Clock::now();
    if (receiverSet.empty()) {
        result.telemetry.totalDurationMs = elapsedMs(protocolStart, Clock::now());
        return result;
    }

    SessionRng rng(
        mConfig.seed ^
        (static_cast<std::uint64_t>(receiverSet.size()) << 32) ^
        static_cast<std::uint64_t>(senderSet.size()));

    const auto okvsSeed = rng.nextU64();
    const auto valueSeed = rng.nextField();

    ProtocolOkvs okvs(mConfig, receiverSet.size(), okvsSeed);
    result.okvsSize = okvs.tableSize();
    result.usedClustering = okvs.usesClustering();
    result.telemetry.okvsSize = result.okvsSize;
    result.telemetry.usedClustering = result.usedClustering;

    std::vector<GF128> receiverBaseValues(receiverSet.size(), GF128::zero());
    std::vector<GF128> senderBaseValues(senderSet.size(), GF128::zero());
    {
        const auto stageStart = Clock::now();
        for (std::size_t i = 0; i < receiverSet.size(); ++i) {
            receiverBaseValues[i] = hashToBaseField(receiverSet[i], valueSeed);
        }
        for (std::size_t i = 0; i < senderSet.size(); ++i) {
            senderBaseValues[i] = hashToBaseField(senderSet[i], valueSeed);
        }

        publishStage(
            result,
            protocolStart,
            PsiStageStat{
                "hash_mapping",
                "Hash Mapping",
                "Mapped receiver and sender items into GF(2^128) base-field values.",
                elapsedMs(stageStart, Clock::now()),
                0,
                false
            },
            onUpdate);
    }

    std::vector<GF128> p;
    {
        const auto stageStart = Clock::now();
        p = okvs.encode(
            receiverSet,
            std::span<const GF128>(receiverBaseValues.data(), receiverBaseValues.size()));
        publishStage(
            result,
            protocolStart,
            PsiStageStat{
                "okvs_encoding",
                "OKVS Encoding",
                "Encoded the receiver set into the OKVS table P.",
                elapsedMs(stageStart, Clock::now()),
                0,
                false
            },
            onUpdate);
    }

    VoleCorrelation vole;
    {
        const auto stageStart = Clock::now();
        vole = generateVoleCorrelation(result.okvsSize, mConfig.numThreads, rng);
        result.usedRealVole = vole.usedRealBackend;
        result.telemetry.usedRealVole = result.usedRealVole;

        publishStage(
            result,
            protocolStart,
            PsiStageStat{
                "vole_generation",
                "VOLE Generation",
                vole.usedRealBackend
                    ? "Measured bytes exchanged on the silent VOLE local socket pair."
                    : "Used the fallback simulated VOLE backend; no transport bytes were measured.",
                elapsedMs(stageStart, Clock::now()),
                vole.transportBytes,
                !vole.usedRealBackend
            },
            onUpdate);
    }

    std::vector<GF128> bPrime(result.okvsSize, GF128::zero());
    std::vector<GF128> receiverDecoded(receiverSet.size(), GF128::zero());
    std::vector<GF128> senderDecoded(senderSet.size(), GF128::zero());
    {
        const auto stageStart = Clock::now();
        for (std::size_t i = 0; i < result.okvsSize; ++i) {
            // In GF(2^128), subtraction equals addition.
            const auto correction = vole.receiverC[i] + p[i];
            bPrime[i] = vole.senderB[i] + correction * vole.delta;
        }

        okvs.decode(
            receiverSet,
            std::span<GF128>(receiverDecoded.data(), receiverDecoded.size()),
            std::span<const GF128>(vole.receiverA.data(), vole.receiverA.size()));

        okvs.decode(senderSet, std::span<GF128>(senderDecoded.data(), senderDecoded.size()), bPrime);

        publishStage(
            result,
            protocolStart,
            PsiStageStat{
                "correlation_transfer",
                "Correction Transfer",
                "Modeled the receiver-to-sender correction payload and decoded both OKVS views.",
                elapsedMs(stageStart, Clock::now()),
                bytesForFieldElements(result.okvsSize),
                true
            },
            onUpdate);
    }

    std::vector<GF128> senderTags(senderSet.size(), GF128::zero());
    {
        const auto stageStart = Clock::now();
        for (std::size_t i = 0; i < senderSet.size(); ++i) {
            const auto senderValue = senderDecoded[i] + vole.delta * senderBaseValues[i];
            senderTags[i] = hashOutput(senderValue);
        }

        std::shuffle(senderTags.begin(), senderTags.end(), std::mt19937_64(rng.nextU64()));

        std::unordered_set<GF128, GF128Hash> senderTagSet;
        senderTagSet.reserve(senderTags.size());
        for (const auto& tag : senderTags) {
            senderTagSet.insert(tag);
        }

        for (std::size_t i = 0; i < receiverDecoded.size(); ++i) {
            if (senderTagSet.contains(hashOutput(receiverDecoded[i]))) {
                result.intersectionIndices.push_back(i);
            }
        }

        publishStage(
            result,
            protocolStart,
            PsiStageStat{
                "intersection_calculation",
                "Intersection Calculation",
                "Modeled the sender tag payload Y' and matched it against receiver tags.",
                elapsedMs(stageStart, Clock::now()),
                bytesForFieldElements(senderTags.size()),
                true
            },
            onUpdate);
    }

    result.telemetry.totalDurationMs = elapsedMs(protocolStart, Clock::now());
    result.telemetry.intersectionSize = result.intersectionIndices.size();
    return result;
}

PsiResult SemiHonestPsi::runTwoPartyLocal(std::span<const KeyView> receiverSet,
                                          std::span<const KeyView> senderSet) const {
    return runTwoPartyLocal(receiverSet, senderSet, {});
}

PsiResult SemiHonestPsi::runTwoPartyLocal(std::span<const KeyView> receiverSet,
                                          std::span<const KeyView> senderSet,
                                          const PsiTelemetryCallback& onUpdate) const {
    PsiResult result;
    result.telemetry.receiverSetSize = receiverSet.size();
    result.telemetry.senderSetSize = senderSet.size();
    const auto protocolStart = Clock::now();
    if (receiverSet.empty()) {
        result.telemetry.totalDurationMs = elapsedMs(protocolStart, Clock::now());
        return result;
    }

    SessionRng rng(
        mConfig.seed ^
        (static_cast<std::uint64_t>(receiverSet.size()) << 32) ^
        static_cast<std::uint64_t>(senderSet.size()));

    const auto okvsSeed = rng.nextU64();
    const auto valueSeed = rng.nextField();

    ProtocolOkvs receiverOkvs(mConfig, receiverSet.size(), okvsSeed);
    ProtocolOkvs senderOkvs(mConfig, receiverSet.size(), okvsSeed);
    result.okvsSize = receiverOkvs.tableSize();
    result.usedClustering = receiverOkvs.usesClustering();
    result.telemetry.okvsSize = result.okvsSize;
    result.telemetry.usedClustering = result.usedClustering;

    std::vector<GF128> receiverBaseValues(receiverSet.size(), GF128::zero());
    std::vector<GF128> senderBaseValues(senderSet.size(), GF128::zero());
    {
        const auto stageStart = Clock::now();
        for (std::size_t i = 0; i < receiverSet.size(); ++i) {
            receiverBaseValues[i] = hashToBaseField(receiverSet[i], valueSeed);
        }
        for (std::size_t i = 0; i < senderSet.size(); ++i) {
            senderBaseValues[i] = hashToBaseField(senderSet[i], valueSeed);
        }

        publishStage(
            result,
            protocolStart,
            PsiStageStat{
                "hash_mapping",
                "Hash Mapping",
                "Mapped receiver and sender items into GF(2^128) base-field values.",
                elapsedMs(stageStart, Clock::now()),
                0,
                false
            },
            onUpdate);
    }

    std::vector<GF128> p;
    {
        const auto stageStart = Clock::now();
        p = receiverOkvs.encode(
            receiverSet,
            std::span<const GF128>(receiverBaseValues.data(), receiverBaseValues.size()));
        publishStage(
            result,
            protocolStart,
            PsiStageStat{
                "okvs_encoding",
                "OKVS Encoding",
                "Encoded the receiver set into the OKVS table P.",
                elapsedMs(stageStart, Clock::now()),
                0,
                false
            },
            onUpdate);
    }

    VoleCorrelation vole;
    {
        const auto stageStart = Clock::now();
        vole = generateVoleCorrelation(result.okvsSize, mConfig.numThreads, rng);
        result.usedRealVole = vole.usedRealBackend;
        result.telemetry.usedRealVole = result.usedRealVole;

        publishStage(
            result,
            protocolStart,
            PsiStageStat{
                "vole_generation",
                "VOLE Generation",
                vole.usedRealBackend
                    ? "Measured bytes exchanged on the silent VOLE local socket pair."
                    : "Used the fallback simulated VOLE backend; no transport bytes were measured.",
                elapsedMs(stageStart, Clock::now()),
                vole.transportBytes,
                !vole.usedRealBackend
            },
            onUpdate);
    }

    std::vector<GF128> receiverDecoded(receiverSet.size(), GF128::zero());
    std::vector<GF128> senderDecoded(senderSet.size(), GF128::zero());
    {
        const auto stageStart = Clock::now();
        const auto tableSize = result.okvsSize;
        auto sockets = coproto::LocalAsyncSocket::makePair();
        std::size_t correctionBytes = 0;
        std::exception_ptr receiverError;
        std::exception_ptr senderError;

        std::thread receiverThread(
            [receiverSocket = std::move(sockets[0]),
             receiverSet,
             tableSize,
             &receiverOkvs,
             &receiverDecoded,
             &vole,
             &p,
             &receiverError,
             &correctionBytes]() mutable {
                try {
                    std::vector<GF128> correction(tableSize, GF128::zero());
                    for (std::size_t i = 0; i < tableSize; ++i) {
                        correction[i] = vole.receiverC[i] + p[i];
                    }

                    correctionBytes = sendFieldVector(
                        receiverSocket,
                        std::span<const GF128>(correction.data(), correction.size()));

                    receiverOkvs.decode(
                        receiverSet,
                        std::span<GF128>(receiverDecoded.data(), receiverDecoded.size()),
                        std::span<const GF128>(vole.receiverA.data(), vole.receiverA.size()));
                } catch (const std::exception& ex) {
                    receiverError = std::make_exception_ptr(
                        std::runtime_error(
                            std::string("correction transfer receiver thread failed: ") + ex.what()));
                } catch (...) {
                    receiverError = std::make_exception_ptr(
                        std::runtime_error("correction transfer receiver thread failed"));
                }
            });

        std::thread senderThread(
            [senderSocket = std::move(sockets[1]),
             senderSet,
             tableSize,
             &senderOkvs,
             &senderDecoded,
             &vole,
             &senderError]() mutable {
                try {
                    const auto correction = recvFieldVector(senderSocket, tableSize);
                    std::vector<GF128> bPrime(tableSize, GF128::zero());
                    for (std::size_t i = 0; i < tableSize; ++i) {
                        bPrime[i] = vole.senderB[i] + correction[i] * vole.delta;
                    }

                    senderOkvs.decode(
                        senderSet,
                        std::span<GF128>(senderDecoded.data(), senderDecoded.size()),
                        std::span<const GF128>(bPrime.data(), bPrime.size()));
                } catch (const std::exception& ex) {
                    senderError = std::make_exception_ptr(
                        std::runtime_error(
                            std::string("correction transfer sender thread failed: ") + ex.what()));
                } catch (...) {
                    senderError = std::make_exception_ptr(
                        std::runtime_error("correction transfer sender thread failed"));
                }
            });

        receiverThread.join();
        senderThread.join();
        if (senderError && receiverError) {
            throw std::runtime_error(
                "correction transfer failed on both parties: sender='" +
                exceptionMessage(senderError) +
                "', receiver='" +
                exceptionMessage(receiverError) +
                "'");
        }
        rethrowIfSet(senderError);
        rethrowIfSet(receiverError);

        publishStage(
            result,
            protocolStart,
            PsiStageStat{
                "correlation_transfer",
                "Correction Transfer",
                "Transferred the receiver correction vector over a local socket and decoded both OKVS views.",
                elapsedMs(stageStart, Clock::now()),
                correctionBytes,
                false
            },
            onUpdate);
    }

    {
        const auto stageStart = Clock::now();
        const auto tagCount = senderSet.size();
        const auto shuffleSeed = rng.nextU64();
        auto sockets = coproto::LocalAsyncSocket::makePair();
        std::size_t tagBytes = 0;
        std::exception_ptr receiverError;
        std::exception_ptr senderError;

        std::thread senderThread(
            [senderSocket = std::move(sockets[0]),
             senderSet,
             shuffleSeed,
             &senderDecoded,
             &senderBaseValues,
             &vole,
             &tagBytes,
             &senderError]() mutable {
                try {
                    std::vector<GF128> senderTags(senderSet.size(), GF128::zero());
                    for (std::size_t i = 0; i < senderSet.size(); ++i) {
                        const auto senderValue = senderDecoded[i] + vole.delta * senderBaseValues[i];
                        senderTags[i] = hashOutput(senderValue);
                    }

                    std::shuffle(senderTags.begin(), senderTags.end(), std::mt19937_64(shuffleSeed));
                    tagBytes = sendFieldVector(
                        senderSocket,
                        std::span<const GF128>(senderTags.data(), senderTags.size()));
                } catch (const std::exception& ex) {
                    senderError = std::make_exception_ptr(
                        std::runtime_error(
                            std::string("intersection sender thread failed: ") + ex.what()));
                } catch (...) {
                    senderError = std::make_exception_ptr(
                        std::runtime_error("intersection sender thread failed"));
                }
            });

        std::thread receiverThread(
            [receiverSocket = std::move(sockets[1]),
             tagCount,
             receiverSet,
             &receiverDecoded,
             &result,
             &receiverError]() mutable {
                try {
                    const auto senderTags = recvFieldVector(receiverSocket, tagCount);
                    std::unordered_set<GF128, GF128Hash> senderTagSet;
                    senderTagSet.reserve(senderTags.size());
                    for (const auto& tag : senderTags) {
                        senderTagSet.insert(tag);
                    }

                    for (std::size_t i = 0; i < receiverSet.size(); ++i) {
                        if (senderTagSet.contains(hashOutput(receiverDecoded[i]))) {
                            result.intersectionIndices.push_back(i);
                        }
                    }
                } catch (const std::exception& ex) {
                    receiverError = std::make_exception_ptr(
                        std::runtime_error(
                            std::string("intersection receiver thread failed: ") + ex.what()));
                } catch (...) {
                    receiverError = std::make_exception_ptr(
                        std::runtime_error("intersection receiver thread failed"));
                }
            });

        senderThread.join();
        receiverThread.join();
        rethrowIfSet(senderError);
        rethrowIfSet(receiverError);

        publishStage(
            result,
            protocolStart,
            PsiStageStat{
                "intersection_calculation",
                "Intersection Calculation",
                "Transferred the sender tag set over a local socket and matched it against receiver tags.",
                elapsedMs(stageStart, Clock::now()),
                tagBytes,
                false
            },
            onUpdate);
    }

    result.telemetry.totalDurationMs = elapsedMs(protocolStart, Clock::now());
    result.telemetry.intersectionSize = result.intersectionIndices.size();
    return result;
}

} // namespace okvs
