#include "okvs/input_dataset.h"

#include <cctype>
#include <string>
#include <unordered_set>

namespace okvs {
namespace {

bool isSeparator(char ch) {
    return ch == '\n' || ch == '\r' || ch == ',' || ch == ';' || ch == '\t';
}

std::string trimAscii(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return std::string(text.substr(begin, end - begin));
}

} // namespace

ParsedItemText parseItemText(std::string_view text) {
    ParsedItemText parsed;
    parsed.items.reserve(std::max<std::size_t>(1, text.size() / 8));

    std::unordered_set<std::string> seen;
    std::size_t tokenStart = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i != text.size() && !isSeparator(text[i])) {
            continue;
        }

        const auto token = trimAscii(text.substr(tokenStart, i - tokenStart));
        if (!token.empty()) {
            ++parsed.rawItemCount;
            if (seen.insert(token).second) {
                parsed.items.push_back(token);
            } else {
                ++parsed.duplicateItemCount;
            }
        } else if (i != tokenStart || !text.empty()) {
            ++parsed.emptyItemCount;
        }
        tokenStart = i + 1;
    }
    return parsed;
}

PreparedPsiDataset preparePsiDataset(std::string_view receiverText,
                                     std::string_view senderText) {
    PreparedPsiDataset prepared;

    const auto receiver = parseItemText(receiverText);
    const auto sender = parseItemText(senderText);

    prepared.receiverItems = receiver.items;
    prepared.senderItems = sender.items;
    prepared.receiverRawCount = receiver.rawItemCount;
    prepared.senderRawCount = sender.rawItemCount;
    prepared.receiverEmptyCount = receiver.emptyItemCount;
    prepared.senderEmptyCount = sender.emptyItemCount;
    prepared.receiverDuplicateCount = receiver.duplicateItemCount;
    prepared.senderDuplicateCount = sender.duplicateItemCount;

    std::unordered_set<std::string> senderSet(
        prepared.senderItems.begin(),
        prepared.senderItems.end());
    for (const auto& item : prepared.receiverItems) {
        if (senderSet.contains(item)) {
            ++prepared.intersectionSize;
        }
    }

    return prepared;
}

} // namespace okvs
