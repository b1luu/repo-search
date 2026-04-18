#include "source_discovery.h"

#include "source_path.h"

#include <algorithm>
#include <system_error>

namespace rs {

std::vector<std::filesystem::path> discover_source_files(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> paths;

    std::error_code ec;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!entry.is_regular_file())
            continue;
        if (!is_supported_source_path(entry.path()))
            continue;
        paths.push_back(normalize_source_path(entry.path()));
    }

    std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
        return a.generic_string() < b.generic_string();
    });

    return paths;
}

} // namespace rs
