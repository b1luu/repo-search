#include "collection_utils.h"

#include <unordered_set>
#include <utility>

namespace rs {

void dedupe_preserve_order(std::vector<std::string>& values) {
    if (values.size() < 2)
        return;

    std::unordered_set<std::string> seen;
    std::vector<std::string> out;
    seen.reserve(values.size());
    out.reserve(values.size());

    for (auto& value : values) {
        if (seen.insert(value).second)
            out.push_back(std::move(value));
    }

    values = std::move(out);
}

} // namespace rs
