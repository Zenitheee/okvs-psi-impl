#include "okvs/input_dataset.h"
#include "okvs/psi.h"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::size_t kMaxDemoSetSize = 1u << 20;
constexpr std::size_t kMaxRequestBodyBytes = 64u << 20;
constexpr std::string_view kTrafficNote =
    "All protocol traffic is measured from local sockets. Silent VOLE uses the coproto "
    "local transport, and the correction vector plus sender tags are exchanged over a "
    "dedicated local socket pair.";

struct ScopedFd {
    ScopedFd() = default;
    explicit ScopedFd(int value)
        : fd(value) {}

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    ScopedFd(ScopedFd&& other) noexcept
        : fd(other.fd) {
        other.fd = -1;
    }

    ScopedFd& operator=(ScopedFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }

    ~ScopedFd() {
        reset();
    }

    void reset(int value = -1) {
        if (fd >= 0) {
            ::close(fd);
        }
        fd = value;
    }

    [[nodiscard]] int get() const {
        return fd;
    }

    [[nodiscard]] explicit operator bool() const {
        return fd >= 0;
    }

private:
    int fd = -1;
};

struct DemoServerConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 8080;
};

struct DemoRequest {
    std::size_t receiverSize = 4096;
    std::size_t senderSize = 4096;
    std::size_t intersectionSize = 512;
    std::size_t numThreads = 4;
    std::size_t binSizeHint = 2048;
    std::uint64_t seed = 0x7265646172746572ULL;
    std::string sessionToken;
};

struct DemoDataset {
    std::vector<std::string> receiverItems;
    std::vector<std::string> senderItems;
};

struct ResolvedDataset {
    DemoDataset dataset;
    bool usesCustomInput = false;
    std::size_t receiverRawCount = 0;
    std::size_t senderRawCount = 0;
    std::size_t receiverEmptyCount = 0;
    std::size_t senderEmptyCount = 0;
    std::size_t receiverDuplicateCount = 0;
    std::size_t senderDuplicateCount = 0;
    std::size_t intersectionSize = 0;
};

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

std::mutex gPreparedDatasetMutex;
std::unordered_map<std::string, okvs::PreparedPsiDataset> gPreparedDatasets;
std::atomic<std::uint64_t> gPreparedDatasetCounter{1};

std::uint64_t splitMix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

std::string hex64(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

std::string makePreparedDatasetToken() {
    const auto counter = gPreparedDatasetCounter.fetch_add(1, std::memory_order_relaxed);
    const auto mixed0 = splitMix64(counter ^ 0x6f6b76732d707369ULL);
    const auto mixed1 = splitMix64(counter ^ 0x6461746173657421ULL);
    return "dataset-" + hex64(mixed0) + hex64(mixed1);
}

std::string jsonEscape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string jsonString(std::string_view value) {
    return "\"" + jsonEscape(value) + "\"";
}

std::string jsonDouble(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

std::string jsonBool(bool value) {
    return value ? "true" : "false";
}

std::string stageToJson(const okvs::PsiStageStat& stage) {
    std::ostringstream stream;
    stream << '{'
           << "\"id\":" << jsonString(stage.id) << ','
           << "\"label\":" << jsonString(stage.label) << ','
           << "\"detail\":" << jsonString(stage.detail) << ','
           << "\"durationMs\":" << jsonDouble(stage.durationMs) << ','
           << "\"networkBytes\":" << stage.networkBytes << ','
           << "\"networkBytesEstimated\":" << jsonBool(stage.networkBytesEstimated)
           << '}';
    return stream.str();
}

std::string telemetryToJson(const okvs::PsiTelemetry& telemetry) {
    std::ostringstream stream;
    stream << '{'
           << "\"totalDurationMs\":" << jsonDouble(telemetry.totalDurationMs) << ','
           << "\"totalNetworkBytes\":" << telemetry.totalNetworkBytes << ','
           << "\"receiverSetSize\":" << telemetry.receiverSetSize << ','
           << "\"senderSetSize\":" << telemetry.senderSetSize << ','
           << "\"okvsSize\":" << telemetry.okvsSize << ','
           << "\"intersectionSize\":" << telemetry.intersectionSize << ','
           << "\"usedClustering\":" << jsonBool(telemetry.usedClustering) << ','
           << "\"usedRealVole\":" << jsonBool(telemetry.usedRealVole) << ','
           << "\"stages\":[";

    for (std::size_t i = 0; i < telemetry.stages.size(); ++i) {
        if (i != 0) {
            stream << ',';
        }
        stream << stageToJson(telemetry.stages[i]);
    }

    stream << "]}";
    return stream.str();
}

template <typename T>
std::string numberArrayToJson(const std::vector<T>& values, std::size_t limit = 0) {
    const auto count = limit == 0 ? values.size() : std::min(values.size(), limit);
    std::ostringstream stream;
    stream << '[';
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) {
            stream << ',';
        }
        stream << values[i];
    }
    stream << ']';
    return stream.str();
}

std::string stringArrayToJson(const std::vector<std::string>& values, std::size_t limit = 0) {
    const auto count = limit == 0 ? values.size() : std::min(values.size(), limit);
    std::ostringstream stream;
    stream << '[';
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) {
            stream << ',';
        }
        stream << jsonString(values[i]);
    }
    stream << ']';
    return stream.str();
}

std::string progressPayloadToJson(const okvs::PsiTelemetry& telemetry) {
    std::ostringstream stream;
    stream << '{'
           << "\"telemetry\":" << telemetryToJson(telemetry) << ','
           << "\"latestStage\":";

    if (telemetry.stages.empty()) {
        stream << "null";
    } else {
        stream << stageToJson(telemetry.stages.back());
    }

    stream << '}';
    return stream.str();
}

std::vector<std::string> previewStrings(const std::vector<std::string>& values, std::size_t limit) {
    const auto count = std::min(values.size(), limit);
    return std::vector<std::string>(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
}

std::vector<std::string> makeIntersectionPreview(const std::vector<std::string>& receiverItems,
                                                 const std::vector<std::size_t>& indices,
                                                 std::size_t limit) {
    std::vector<std::string> preview;
    preview.reserve(std::min(indices.size(), limit));
    for (std::size_t i = 0; i < indices.size() && preview.size() < limit; ++i) {
        preview.push_back(receiverItems[indices[i]]);
    }
    return preview;
}

std::vector<std::size_t> previewIndices(const std::vector<std::size_t>& values, std::size_t limit) {
    const auto count = std::min(values.size(), limit);
    return std::vector<std::size_t>(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
}

std::string datasetSummaryToJson(const ResolvedDataset& resolved) {
    std::ostringstream stream;
    stream << '{'
           << "\"mode\":" << jsonString(resolved.usesCustomInput ? "custom" : "synthetic") << ','
           << "\"receiverRawCount\":" << resolved.receiverRawCount << ','
           << "\"senderRawCount\":" << resolved.senderRawCount << ','
           << "\"receiverEmptyCount\":" << resolved.receiverEmptyCount << ','
           << "\"senderEmptyCount\":" << resolved.senderEmptyCount << ','
           << "\"receiverDuplicateCount\":" << resolved.receiverDuplicateCount << ','
           << "\"senderDuplicateCount\":" << resolved.senderDuplicateCount << ','
           << "\"preparedIntersectionSize\":" << resolved.intersectionSize
           << '}';
    return stream.str();
}

std::string resultPayloadToJson(const DemoRequest& request,
                                const ResolvedDataset& resolved,
                                const okvs::PsiResult& result) {
    const auto& dataset = resolved.dataset;
    const auto receiverPreview = previewStrings(dataset.receiverItems, 8);
    const auto senderPreview = previewStrings(dataset.senderItems, 8);
    const auto intersectionPreview = makeIntersectionPreview(
        dataset.receiverItems,
        result.intersectionIndices,
        12);
    const auto indexPreview = previewIndices(result.intersectionIndices, 12);

    std::ostringstream stream;
    stream << '{'
           << "\"receiverSize\":" << request.receiverSize << ','
           << "\"senderSize\":" << request.senderSize << ','
           << "\"requestedIntersectionSize\":" << request.intersectionSize << ','
           << "\"actualIntersectionSize\":" << result.intersectionIndices.size() << ','
           << "\"okvsSize\":" << result.okvsSize << ','
           << "\"numThreads\":" << request.numThreads << ','
           << "\"binSizeHint\":" << request.binSizeHint << ','
           << "\"seed\":" << request.seed << ','
           << "\"usedClustering\":" << jsonBool(result.usedClustering) << ','
           << "\"usedRealVole\":" << jsonBool(result.usedRealVole) << ','
           << "\"trafficNote\":" << jsonString(kTrafficNote) << ','
           << "\"datasetSummary\":" << datasetSummaryToJson(resolved) << ','
           << "\"receiverPreview\":" << stringArrayToJson(receiverPreview) << ','
           << "\"senderPreview\":" << stringArrayToJson(senderPreview) << ','
           << "\"intersectionIndexPreview\":" << numberArrayToJson(indexPreview) << ','
           << "\"intersectionPreview\":" << stringArrayToJson(intersectionPreview) << ','
           << "\"telemetry\":" << telemetryToJson(result.telemetry)
           << '}';
    return stream.str();
}

std::string readyPayloadToJson(const DemoRequest& request, const ResolvedDataset& resolved) {
    std::ostringstream stream;
    stream << '{'
           << "\"receiverSize\":" << request.receiverSize << ','
           << "\"senderSize\":" << request.senderSize << ','
           << "\"intersectionSize\":" << request.intersectionSize << ','
           << "\"numThreads\":" << request.numThreads << ','
           << "\"binSizeHint\":" << request.binSizeHint << ','
           << "\"seed\":" << request.seed << ','
           << "\"datasetSummary\":" << datasetSummaryToJson(resolved) << ','
           << "\"trafficNote\":" << jsonString(kTrafficNote)
           << '}';
    return stream.str();
}

std::vector<okvs::KeyView> makeViews(const std::vector<std::string>& items) {
    std::vector<okvs::KeyView> views(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        views[i] = okvs::KeyView{
            reinterpret_cast<const std::uint8_t*>(items[i].data()),
            items[i].size()
        };
    }
    return views;
}

std::string makeItem(std::string_view prefix,
                     std::uint64_t seed,
                     std::uint64_t domainTag,
                     std::size_t index) {
    const auto left = splitMix64(seed + domainTag + static_cast<std::uint64_t>(index) * 2);
    const auto right = splitMix64(seed ^ domainTag ^ (static_cast<std::uint64_t>(index) * 2 + 1));
    return std::string(prefix) + "-" + hex64(left) + hex64(right);
}

DemoDataset makeDataset(const DemoRequest& request) {
    if (request.intersectionSize > std::min(request.receiverSize, request.senderSize)) {
        throw std::runtime_error("Intersection size cannot exceed either party size.");
    }

    DemoDataset dataset;
    dataset.receiverItems.reserve(request.receiverSize);
    dataset.senderItems.reserve(request.senderSize);

    std::vector<std::string> sharedItems;
    sharedItems.reserve(request.intersectionSize);
    for (std::size_t i = 0; i < request.intersectionSize; ++i) {
        sharedItems.push_back(makeItem("shared", request.seed, 0x1000ULL, i));
    }

    dataset.receiverItems.insert(dataset.receiverItems.end(), sharedItems.begin(), sharedItems.end());
    dataset.senderItems.insert(dataset.senderItems.end(), sharedItems.begin(), sharedItems.end());

    for (std::size_t i = request.intersectionSize; i < request.receiverSize; ++i) {
        dataset.receiverItems.push_back(makeItem("receiver", request.seed, 0x2000ULL, i));
    }
    for (std::size_t i = request.intersectionSize; i < request.senderSize; ++i) {
        dataset.senderItems.push_back(makeItem("sender", request.seed, 0x3000ULL, i));
    }

    std::mt19937_64 receiverShuffle(splitMix64(request.seed ^ 0x515349ULL));
    std::mt19937_64 senderShuffle(splitMix64(request.seed ^ 0x0badd00dULL));
    std::shuffle(dataset.receiverItems.begin(), dataset.receiverItems.end(), receiverShuffle);
    std::shuffle(dataset.senderItems.begin(), dataset.senderItems.end(), senderShuffle);

    return dataset;
}

bool sendAll(int fd, std::string_view data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto result = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

bool sendResponse(int fd,
                  std::string_view status,
                  std::string_view contentType,
                  std::string_view body) {
    std::ostringstream stream;
    stream << "HTTP/1.1 " << status << "\r\n"
           << "Content-Type: " << contentType << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n"
           << "Cache-Control: no-store\r\n\r\n";
    return sendAll(fd, stream.str()) && sendAll(fd, body);
}

bool sendFileResponse(int fd,
                      const std::filesystem::path& path,
                      std::string_view contentType) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return sendResponse(fd, "404 Not Found", "text/plain; charset=utf-8", "Missing asset.\n");
    }

    std::ostringstream body;
    body << file.rdbuf();
    return sendResponse(fd, "200 OK", contentType, body.str());
}

bool startEventStream(int fd) {
    return sendAll(
        fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n"
        "X-Accel-Buffering: no\r\n\r\n");
}

bool sendSseEvent(int fd, std::string_view eventName, std::string_view payload) {
    std::ostringstream stream;
    stream << "event: " << eventName << "\n"
           << "data: " << payload << "\n\n";
    return sendAll(fd, stream.str());
}

std::string trimAscii(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

std::string toLowerAscii(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

std::optional<HttpRequest> parseRequestHead(const std::string& rawHead) {
    const auto lineEnd = rawHead.find("\r\n");
    if (lineEnd == std::string::npos) {
        return std::nullopt;
    }

    std::istringstream requestLine(rawHead.substr(0, lineEnd));
    HttpRequest request;
    std::string target;
    std::string version;
    if (!(requestLine >> request.method >> target >> version)) {
        return std::nullopt;
    }

    const auto queryPos = target.find('?');
    request.path = target.substr(0, queryPos);
    if (queryPos != std::string::npos) {
        request.query = target.substr(queryPos + 1);
    }

    std::size_t headerStart = lineEnd + 2;
    while (headerStart < rawHead.size()) {
        const auto headerEnd = rawHead.find("\r\n", headerStart);
        if (headerEnd == std::string::npos) {
            break;
        }
        if (headerEnd == headerStart) {
            break;
        }

        const auto separator = rawHead.find(':', headerStart);
        if (separator != std::string::npos && separator < headerEnd) {
            const auto name = toLowerAscii(rawHead.substr(headerStart, separator - headerStart));
            const auto value = trimAscii(rawHead.substr(separator + 1, headerEnd - separator - 1));
            request.headers[name] = value;
        }
        headerStart = headerEnd + 2;
    }

    return request;
}

std::optional<HttpRequest> readRequest(int fd) {
    std::string rawRequest;
    rawRequest.reserve(4096);

    char buffer[4096];
    std::size_t headerEnd = std::string::npos;
    while ((headerEnd = rawRequest.find("\r\n\r\n")) == std::string::npos) {
        const auto received = ::recv(fd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return std::nullopt;
        }
        rawRequest.append(buffer, static_cast<std::size_t>(received));
        if (rawRequest.size() > 64 * 1024) {
            return std::nullopt;
        }
    }

    auto request = parseRequestHead(rawRequest.substr(0, headerEnd));
    if (!request) {
        return std::nullopt;
    }

    std::size_t contentLength = 0;
    if (const auto iter = request->headers.find("content-length"); iter != request->headers.end()) {
        try {
            contentLength = static_cast<std::size_t>(std::stoull(iter->second));
        } catch (const std::exception&) {
            throw std::runtime_error("Invalid Content-Length header.");
        }
    }

    if (contentLength > kMaxRequestBodyBytes) {
        throw std::runtime_error("Request body exceeds the demo limit.");
    }

    request->body = rawRequest.substr(headerEnd + 4);
    while (request->body.size() < contentLength) {
        const auto received = ::recv(fd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return std::nullopt;
        }
        request->body.append(buffer, static_cast<std::size_t>(received));
        if (request->body.size() > kMaxRequestBodyBytes) {
            throw std::runtime_error("Request body exceeds the demo limit.");
        }
    }

    if (request->body.size() > contentLength) {
        request->body.resize(contentLength);
    }

    return request;
}

int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

std::string urlDecode(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            decoded.push_back(' ');
            continue;
        }
        if (value[i] == '%' && i + 2 < value.size()) {
            const auto hi = hexValue(value[i + 1]);
            const auto lo = hexValue(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(value[i]);
    }
    return decoded;
}

std::unordered_map<std::string, std::string> parseParameterString(std::string_view encoded) {
    std::unordered_map<std::string, std::string> values;
    std::size_t start = 0;
    while (start < encoded.size()) {
        const auto separator = encoded.find('&', start);
        const auto token = encoded.substr(
            start,
            separator == std::string_view::npos ? encoded.size() - start : separator - start);
        const auto equals = token.find('=');
        if (equals != std::string_view::npos) {
            values[urlDecode(token.substr(0, equals))] = urlDecode(token.substr(equals + 1));
        }
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return values;
}

std::unordered_map<std::string, std::string> parseQuery(std::string_view query) {
    return parseParameterString(query);
}

std::uint64_t parseU64(std::string_view name,
                       const std::unordered_map<std::string, std::string>& query,
                       std::uint64_t fallback) {
    const auto iter = query.find(std::string(name));
    if (iter == query.end() || iter->second.empty()) {
        return fallback;
    }

    try {
        return std::stoull(iter->second);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid numeric value for " + std::string(name) + ".");
    }
}

DemoRequest parseDemoRequest(std::string_view queryString) {
    const auto query = parseQuery(queryString);

    DemoRequest request;
    request.receiverSize = static_cast<std::size_t>(parseU64("receiverSize", query, request.receiverSize));
    request.senderSize = static_cast<std::size_t>(parseU64("senderSize", query, request.senderSize));
    request.intersectionSize = static_cast<std::size_t>(parseU64("intersectionSize", query, request.intersectionSize));
    request.numThreads = static_cast<std::size_t>(parseU64("numThreads", query, request.numThreads));
    request.binSizeHint = static_cast<std::size_t>(parseU64("binSizeHint", query, request.binSizeHint));
    request.seed = parseU64("seed", query, request.seed);
    if (const auto iter = query.find("token"); iter != query.end()) {
        request.sessionToken = iter->second;
    }

    if (request.numThreads == 0) {
        throw std::runtime_error("numThreads must be positive.");
    }
    if (request.sessionToken.empty() &&
        (request.receiverSize > kMaxDemoSetSize || request.senderSize > kMaxDemoSetSize)) {
        throw std::runtime_error("Set sizes above 2^20 are disabled in the demo.");
    }
    return request;
}

ResolvedDataset resolveDataset(DemoRequest& request) {
    ResolvedDataset resolved;
    if (request.sessionToken.empty()) {
        resolved.dataset = makeDataset(request);
        resolved.receiverRawCount = request.receiverSize;
        resolved.senderRawCount = request.senderSize;
        resolved.intersectionSize = request.intersectionSize;
        return resolved;
    }

    okvs::PreparedPsiDataset prepared;
    {
        std::scoped_lock lock(gPreparedDatasetMutex);
        const auto iter = gPreparedDatasets.find(request.sessionToken);
        if (iter == gPreparedDatasets.end()) {
            throw std::runtime_error("The uploaded dataset session was not found or has expired.");
        }
        prepared = iter->second;
    }

    resolved.dataset.receiverItems = std::move(prepared.receiverItems);
    resolved.dataset.senderItems = std::move(prepared.senderItems);
    resolved.usesCustomInput = true;
    resolved.receiverRawCount = prepared.receiverRawCount;
    resolved.senderRawCount = prepared.senderRawCount;
    resolved.receiverEmptyCount = prepared.receiverEmptyCount;
    resolved.senderEmptyCount = prepared.senderEmptyCount;
    resolved.receiverDuplicateCount = prepared.receiverDuplicateCount;
    resolved.senderDuplicateCount = prepared.senderDuplicateCount;
    resolved.intersectionSize = prepared.intersectionSize;

    request.receiverSize = resolved.dataset.receiverItems.size();
    request.senderSize = resolved.dataset.senderItems.size();
    request.intersectionSize = resolved.intersectionSize;

    if (request.receiverSize > kMaxDemoSetSize || request.senderSize > kMaxDemoSetSize) {
        throw std::runtime_error("Uploaded sets above 2^20 items are disabled in the demo.");
    }

    return resolved;
}

std::string sessionPayloadToJson(const std::string& token,
                                 const okvs::PreparedPsiDataset& prepared) {
    std::ostringstream stream;
    stream << '{'
           << "\"token\":" << jsonString(token) << ','
           << "\"receiverSize\":" << prepared.receiverItems.size() << ','
           << "\"senderSize\":" << prepared.senderItems.size() << ','
           << "\"intersectionSize\":" << prepared.intersectionSize << ','
           << "\"receiverRawCount\":" << prepared.receiverRawCount << ','
           << "\"senderRawCount\":" << prepared.senderRawCount << ','
           << "\"receiverEmptyCount\":" << prepared.receiverEmptyCount << ','
           << "\"senderEmptyCount\":" << prepared.senderEmptyCount << ','
           << "\"receiverDuplicateCount\":" << prepared.receiverDuplicateCount << ','
           << "\"senderDuplicateCount\":" << prepared.senderDuplicateCount
           << '}';
    return stream.str();
}

std::string requireParameter(std::string_view name,
                             const std::unordered_map<std::string, std::string>& values) {
    const auto iter = values.find(std::string(name));
    if (iter == values.end()) {
        throw std::runtime_error("Missing required parameter " + std::string(name) + ".");
    }
    return iter->second;
}

std::filesystem::path assetPath(std::string_view fileName) {
    return std::filesystem::path(VOLEPSI2_DEMO_ASSET_DIR) / fileName;
}

void handleDatasetSessionRequest(int fd, const HttpRequest& request) {
    if (request.method != "POST") {
        sendResponse(
            fd,
            "405 Method Not Allowed",
            "text/plain; charset=utf-8",
            "Only POST is supported for dataset uploads.\n");
        return;
    }

    const auto form = parseParameterString(request.body);
    const auto receiverText = requireParameter("receiverText", form);
    const auto senderText = requireParameter("senderText", form);
    const auto prepared = okvs::preparePsiDataset(receiverText, senderText);

    if (prepared.receiverItems.empty() && prepared.senderItems.empty()) {
        throw std::runtime_error("Both uploaded datasets are empty after preprocessing.");
    }
    if (prepared.receiverItems.size() > kMaxDemoSetSize || prepared.senderItems.size() > kMaxDemoSetSize) {
        throw std::runtime_error("Uploaded sets above 2^20 items are disabled in the demo.");
    }

    const auto token = makePreparedDatasetToken();
    const auto body = sessionPayloadToJson(token, prepared);
    {
        std::scoped_lock lock(gPreparedDatasetMutex);
        gPreparedDatasets.emplace(token, prepared);
    }

    sendResponse(fd, "200 OK", "application/json; charset=utf-8", body);
}

void runDemoStream(int fd, DemoRequest request) {
    if (!startEventStream(fd)) {
        return;
    }

    try {
        const auto resolved = resolveDataset(request);
        if (!sendSseEvent(fd, "ready", readyPayloadToJson(request, resolved))) {
            return;
        }

        auto receiverViews = makeViews(resolved.dataset.receiverItems);
        auto senderViews = makeViews(resolved.dataset.senderItems);

        okvs::PsiConfig config;
        config.binSizeHint = request.binSizeHint;
        config.numThreads = request.numThreads;
        config.seed = request.seed;

        okvs::SemiHonestPsi psi(config);
        const auto result = psi.runTwoPartyLocal(
            std::span<const okvs::KeyView>(receiverViews.data(), receiverViews.size()),
            std::span<const okvs::KeyView>(senderViews.data(), senderViews.size()),
            [&](const okvs::PsiTelemetry& telemetry) {
                sendSseEvent(fd, "progress", progressPayloadToJson(telemetry));
            });

        sendSseEvent(fd, "result", resultPayloadToJson(request, resolved, result));
    } catch (const std::exception& ex) {
        sendSseEvent(
            fd,
            "failed",
            "{\"message\":" + jsonString(ex.what()) + "}");
    }
}

DemoServerConfig parseArgs(int argc, char** argv) {
    DemoServerConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help") {
            std::cout
                << "Usage: volepsi2_demo [--host 127.0.0.1] [--port 8080]\n"
                << "Serves the volepsi2 PSI demo interface with live telemetry.\n";
            std::exit(0);
        }
        if (i + 1 >= argc) {
            throw std::runtime_error("Missing value for argument " + std::string(arg) + ".");
        }

        const auto value = argv[++i];
        if (arg == "--host") {
            config.host = value;
        } else if (arg == "--port") {
            config.port = static_cast<std::uint16_t>(std::stoul(value));
        } else {
            throw std::runtime_error("Unknown argument " + std::string(arg) + ".");
        }
    }
    return config;
}

ScopedFd bindListener(const DemoServerConfig& config, std::uint16_t& boundPort) {
    const std::vector<std::uint16_t> candidatePorts =
        config.port == 0 ? std::vector<std::uint16_t>{8080, 8081, 8082, 8083, 0}
                         : std::vector<std::uint16_t>{config.port};

    for (const auto port : candidatePorts) {
        ScopedFd fd(::socket(AF_INET, SOCK_STREAM, 0));
        if (!fd) {
            throw std::runtime_error("Failed to create listening socket.");
        }

        int reuseAddr = 1;
        ::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (::inet_pton(AF_INET, config.host.c_str(), &address.sin_addr) != 1) {
            throw std::runtime_error("Only numeric IPv4 hosts are supported, e.g. 127.0.0.1.");
        }

        if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            if (config.port == 0) {
                continue;
            }
            throw std::runtime_error("Failed to bind port " + std::to_string(port) + ".");
        }

        if (::listen(fd.get(), 32) != 0) {
            throw std::runtime_error("Failed to listen on the demo socket.");
        }

        sockaddr_in boundAddress{};
        socklen_t addressSize = sizeof(boundAddress);
        if (::getsockname(fd.get(), reinterpret_cast<sockaddr*>(&boundAddress), &addressSize) != 0) {
            throw std::runtime_error("Failed to inspect the bound demo port.");
        }

        boundPort = ntohs(boundAddress.sin_port);
        return fd;
    }

    throw std::runtime_error("Failed to bind any demo port.");
}

void handleStaticRequest(int fd, std::string_view path) {
    if (path == "/" || path == "/index.html") {
        sendFileResponse(fd, assetPath("index.html"), "text/html; charset=utf-8");
        return;
    }
    if (path == "/styles.css") {
        sendFileResponse(fd, assetPath("styles.css"), "text/css; charset=utf-8");
        return;
    }
    if (path == "/app.js") {
        sendFileResponse(fd, assetPath("app.js"), "application/javascript; charset=utf-8");
        return;
    }
    sendResponse(fd, "404 Not Found", "text/plain; charset=utf-8", "Not found.\n");
}

void handleConnection(int clientFd) {
    ScopedFd client(clientFd);
    std::optional<HttpRequest> request;
    try {
        request = readRequest(client.get());
    } catch (const std::exception& ex) {
        sendResponse(
            client.get(),
            "400 Bad Request",
            "text/plain; charset=utf-8",
            std::string(ex.what()) + "\n");
        return;
    }
    if (!request) {
        return;
    }

    if (request->path == "/api/session") {
        try {
            handleDatasetSessionRequest(client.get(), *request);
        } catch (const std::exception& ex) {
            sendResponse(
                client.get(),
                "400 Bad Request",
                "text/plain; charset=utf-8",
                std::string(ex.what()) + "\n");
        }
        return;
    }

    if (request->method != "GET") {
        sendResponse(
            client.get(),
            "405 Method Not Allowed",
            "text/plain; charset=utf-8",
            "Only GET is supported.\n");
        return;
    }

    if (request->path == "/api/run") {
        try {
            runDemoStream(client.get(), parseDemoRequest(request->query));
        } catch (const std::exception& ex) {
            sendResponse(
                client.get(),
                "400 Bad Request",
                "text/plain; charset=utf-8",
                std::string(ex.what()) + "\n");
        }
        return;
    }

    handleStaticRequest(client.get(), request->path);
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);

    try {
        const auto config = parseArgs(argc, argv);
        std::uint16_t port = 0;
        auto listener = bindListener(config, port);

        std::cout << "volepsi2 demo listening on http://" << config.host << ':' << port << '\n';
        std::cout << "Press Ctrl+C to stop the server.\n";

        while (true) {
            sockaddr_in clientAddress{};
            socklen_t addressSize = sizeof(clientAddress);
            const auto clientFd = ::accept(
                listener.get(),
                reinterpret_cast<sockaddr*>(&clientAddress),
                &addressSize);
            if (clientFd < 0) {
                continue;
            }

            std::thread(handleConnection, clientFd).detach();
        }
    } catch (const std::exception& ex) {
        std::cerr << "Demo server error: " << ex.what() << '\n';
        return 1;
    }
}
