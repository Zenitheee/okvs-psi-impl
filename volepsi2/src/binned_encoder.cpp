#include "okvs/binned_encoder.h"

#include "hash_utils.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace okvs {
namespace {

long double binaryRelativeEntropy(long double x, long double p) {
    if (x <= 0.0L || x >= 1.0L || p <= 0.0L || p >= 1.0L) {
        if (x == p) {
            return 0.0L;
        }
        if (x == 1.0L) {
            return -std::log(p);
        }
        if (x == 0.0L) {
            return -std::log1pl(-p);
        }
        throw std::runtime_error("Invalid Bernoulli parameters for relative entropy.");
    }

    return x * std::log(x / p) + (1.0L - x) * std::log((1.0L - x) / (1.0L - p));
}

bool clusterCapSatisfiesTailBound(std::size_t numItems,
                                  std::size_t numBins,
                                  std::size_t securityParameter,
                                  std::size_t cap) {
    if (cap >= numItems) {
        return true;
    }
    if (numBins <= 1) {
        return cap >= numItems;
    }

    const auto threshold = cap + 1;
    const long double p = 1.0L / static_cast<long double>(numBins);
    const long double x = static_cast<long double>(threshold) / static_cast<long double>(numItems);
    if (x <= p) {
        return false;
    }

    const long double logPerClusterTail =
        -static_cast<long double>(numItems) * binaryRelativeEntropy(x, p);
    const long double logUnionBound =
        std::log(static_cast<long double>(numBins)) + logPerClusterTail;
    const long double logTarget =
        -static_cast<long double>(securityParameter) * std::log(2.0L);
    return logUnionBound <= logTarget;
}

std::span<const std::uint8_t> keyBytes(KeyView key) {
    if (key.size == 0 || key.data == nullptr) {
        return {};
    }
    return std::span<const std::uint8_t>(key.data, key.size);
}

GF128 decodeClusteredWithHasher(const RowHasher& hasher,
                                KeyView key,
                                std::span<const GF128> table,
                                std::size_t sparseOffset,
                                std::size_t denseOffset,
                                std::size_t sparseColumns,
                                std::size_t denseColumns) {
    if (key.size > 0 && key.data == nullptr) {
        throw std::runtime_error("KeyView points to null data.");
    }

    const RowData row = hasher.generate(keyBytes(key));
    GF128 acc = GF128::zero();

    for (const auto idx : row.sparse) {
        if (idx >= sparseColumns) {
            throw std::runtime_error("Sparse index out of range during clustered decode.");
        }
        acc += table[sparseOffset + idx];
    }

    for (std::size_t j = 0; j < row.dense.size(); ++j) {
        if (j >= denseColumns) {
            throw std::runtime_error("Dense index out of range during clustered decode.");
        }
        acc += row.dense[j] * table[denseOffset + j];
    }

    return acc;
}

} // namespace

BinnedOkvsEncoder::BinnedOkvsEncoder(OkvsConfig config,
                                     std::uint64_t seed,
                                     std::size_t numItems,
                                     std::size_t binSizeHint)
    : mConfig(config),
      mSeed(seed),
      mNumItems(numItems),
      mNumBins(computeNumBins(numItems, binSizeHint)),
      mItemsPerBin(computeItemsPerBin(numItems, mNumBins, config.securityParameter)),
      mClusterEncoder([&] {
          OkvsConfig fixed = config;
          OkvsEncoder probe(config, seed);
          fixed.explicitSparseSize = probe.sparseSize(mItemsPerBin);
          fixed.explicitDenseSize = probe.denseSize(mItemsPerBin);
          return OkvsEncoder(fixed, seed);
      }()) {
    mSparsePerBin = mClusterEncoder.sparseSize(mItemsPerBin);
    mDensePerBin = mClusterEncoder.denseSize(mItemsPerBin);
    mTotalSparseSize = mNumBins * mSparsePerBin;
    mTotalDenseSize = mNumBins * mDensePerBin;
    mClusterTableSize = mSparsePerBin + mDensePerBin;
}

std::size_t BinnedOkvsEncoder::totalSize() const { return mTotalSparseSize + mTotalDenseSize; }
std::size_t BinnedOkvsEncoder::numBins() const { return mNumBins; }
std::size_t BinnedOkvsEncoder::itemsPerBin() const { return mItemsPerBin; }
std::size_t BinnedOkvsEncoder::sparsePerBin() const { return mSparsePerBin; }
std::size_t BinnedOkvsEncoder::densePerBin() const { return mDensePerBin; }

std::size_t BinnedOkvsEncoder::computeNumBins(std::size_t numItems, std::size_t binSizeHint) {
    if (numItems == 0) return 1;
    if (binSizeHint == 0) return 1;
    return std::max<std::size_t>(1, (numItems + binSizeHint - 1) / binSizeHint);
}

std::size_t BinnedOkvsEncoder::computeItemsPerBin(std::size_t numItems, std::size_t numBins, std::size_t ssp) {
    if (numItems == 0) return 1;
    if (numBins == 0) throw std::runtime_error("numBins must be non-zero.");
    if (numBins == 1) return numItems;

    std::size_t lower = std::max<std::size_t>(1, (numItems + numBins - 1) / numBins);
    std::size_t upper = numItems;
    while (lower < upper) {
        const auto mid = lower + (upper - lower) / 2;
        if (clusterCapSatisfiesTailBound(numItems, numBins, ssp, mid)) {
            upper = mid;
        } else {
            lower = mid + 1;
        }
    }

    return lower;
}

std::uint64_t BinnedOkvsEncoder::hashKey(KeyView key, std::uint64_t seed) {
    return internal::hashBytesToU64(
        keyBytes(key),
        seed,
        0x38b34ae59d1228d9ULL,
        internal::HashDomain::BinnedKey);
}

std::size_t BinnedOkvsEncoder::clusterForKey(KeyView key) const {
    if (mNumBins == 0) {
        throw std::runtime_error("Cluster count must be non-zero.");
    }
    if (key.size > 0 && key.data == nullptr) {
        throw std::runtime_error("KeyView points to null data.");
    }
    return hashKey(key, mSeed) % mNumBins;
}

std::size_t BinnedOkvsEncoder::sparseOffset(std::size_t cluster) const {
    return cluster * mSparsePerBin;
}

std::size_t BinnedOkvsEncoder::denseBaseOffset() const {
    return mTotalSparseSize;
}

std::size_t BinnedOkvsEncoder::denseOffset(std::size_t cluster) const {
    return denseBaseOffset() + cluster * mDensePerBin;
}

void BinnedOkvsEncoder::encode(std::span<const KeyView> keys,
                               std::span<const GF128> values,
                               std::span<GF128> output,
                               std::size_t numThreads) const {
    if (keys.size() != values.size()) {
        throw std::runtime_error("Mismatched key/value vector sizes.");
    }
    if (output.size() != totalSize()) {
        throw std::runtime_error("Output buffer size mismatch for binned encoding.");
    }

    std::fill(output.begin(), output.end(), GF128::zero());

    std::vector<std::vector<std::size_t>> clusters(mNumBins);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].size > 0 && keys[i].data == nullptr) {
            throw std::runtime_error("KeyView points to null data.");
        }
        const auto cluster = clusterForKey(keys[i]);
        clusters[cluster].push_back(i);
    }

    for (std::size_t cluster = 0; cluster < mNumBins; ++cluster) {
        if (clusters[cluster].size() > mItemsPerBin) {
            throw std::runtime_error("Cluster overflow in clustered OKVS encoder.");
        }
    }

    const std::size_t threads = std::max<std::size_t>(1, numThreads);
    std::atomic<std::size_t> nextCluster{0};
    std::exception_ptr workerError = nullptr;
    std::mutex workerErrorMutex;

    auto worker = [&]() {
        try {
            while (true) {
                const auto cluster = nextCluster.fetch_add(1, std::memory_order_relaxed);
                if (cluster >= mNumBins) {
                    break;
                }

                const auto count = clusters[cluster].size();
                if (count == 0) {
                    continue;
                }

                std::vector<KeyView> clusterKeys(count);
                std::vector<GF128> clusterValues(count);
                for (std::size_t j = 0; j < count; ++j) {
                    const auto idx = clusters[cluster][j];
                    clusterKeys[j] = keys[idx];
                    clusterValues[j] = values[idx];
                }

                auto table = mClusterEncoder.encode(
                    std::span<const KeyView>(clusterKeys.data(), clusterKeys.size()),
                    std::span<const GF128>(clusterValues.data(), clusterValues.size()));
                if (table.data.size() != mClusterTableSize) {
                    throw std::runtime_error("Unexpected per-cluster encoded size.");
                }

                auto sparseBlock = output.subspan(sparseOffset(cluster), mSparsePerBin);
                auto denseBlock = output.subspan(denseOffset(cluster), mDensePerBin);
                std::copy_n(table.data.begin(), mSparsePerBin, sparseBlock.begin());
                std::copy_n(table.data.begin() + static_cast<std::ptrdiff_t>(mSparsePerBin),
                            mDensePerBin,
                            denseBlock.begin());
            }
        } catch (...) {
            std::scoped_lock lock(workerErrorMutex);
            if (!workerError) {
                workerError = std::current_exception();
            }
        }
    };

    if (threads == 1 || mNumBins == 1) {
        worker();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(threads);
        for (std::size_t t = 0; t < threads; ++t) {
            pool.emplace_back(worker);
        }
        for (auto& th : pool) {
            th.join();
        }
    }

    if (workerError) {
        std::rethrow_exception(workerError);
    }
}

GF128 BinnedOkvsEncoder::decode(KeyView key, std::span<const GF128> table) const {
    if (table.size() != totalSize()) {
        throw std::runtime_error("Binned table size mismatch.");
    }
    if (key.size > 0 && key.data == nullptr) {
        throw std::runtime_error("KeyView points to null data.");
    }

    const auto cluster = clusterForKey(key);
    const RowHasher hasher(mSparsePerBin, mDensePerBin, mConfig.weight, mSeed);
    return decodeClusteredWithHasher(
        hasher,
        key,
        table,
        sparseOffset(cluster),
        denseOffset(cluster),
        mSparsePerBin,
        mDensePerBin);
}

void BinnedOkvsEncoder::decode(std::span<const KeyView> keys,
                               std::span<GF128> values,
                               std::span<const GF128> table,
                               std::size_t numThreads) const {
    if (keys.size() != values.size()) {
        throw std::runtime_error("Mismatched key/output sizes for binned decode.");
    }
    if (table.size() != totalSize()) {
        throw std::runtime_error("Binned table size mismatch.");
    }

    const std::size_t threads = std::max<std::size_t>(1, numThreads);
    std::atomic<std::size_t> next{0};
    std::exception_ptr workerError = nullptr;
    std::mutex workerErrorMutex;
    const RowHasher hasher(mSparsePerBin, mDensePerBin, mConfig.weight, mSeed);

    auto worker = [&]() {
        try {
            while (true) {
                const auto i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= keys.size()) {
                    break;
                }
                const auto cluster = clusterForKey(keys[i]);
                values[i] = decodeClusteredWithHasher(
                    hasher,
                    keys[i],
                    table,
                    sparseOffset(cluster),
                    denseOffset(cluster),
                    mSparsePerBin,
                    mDensePerBin);
            }
        } catch (...) {
            std::scoped_lock lock(workerErrorMutex);
            if (!workerError) {
                workerError = std::current_exception();
            }
        }
    };

    if (threads == 1 || keys.size() < 2) {
        worker();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(threads);
        for (std::size_t t = 0; t < threads; ++t) {
            pool.emplace_back(worker);
        }
        for (auto& th : pool) {
            th.join();
        }
    }

    if (workerError) {
        std::rethrow_exception(workerError);
    }
}

} // namespace okvs
