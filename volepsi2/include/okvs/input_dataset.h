#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace okvs {

struct ParsedItemText {
    std::vector<std::string> items;
    std::size_t rawItemCount = 0;
    std::size_t emptyItemCount = 0;
    std::size_t duplicateItemCount = 0;
};

struct PreparedPsiDataset {
    std::vector<std::string> receiverItems;
    std::vector<std::string> senderItems;
    std::size_t receiverRawCount = 0;
    std::size_t senderRawCount = 0;
    std::size_t receiverEmptyCount = 0;
    std::size_t senderEmptyCount = 0;
    std::size_t receiverDuplicateCount = 0;
    std::size_t senderDuplicateCount = 0;
    std::size_t intersectionSize = 0;
};

[[nodiscard]] ParsedItemText parseItemText(std::string_view text);
[[nodiscard]] PreparedPsiDataset preparePsiDataset(std::string_view receiverText,
                                                   std::string_view senderText);

} // namespace okvs
