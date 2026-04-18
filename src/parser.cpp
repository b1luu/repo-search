#include "parser.h"

#include "file_reader.h"
#include "parsed_file_builder.h"
#include "source_discovery.h"

#include <optional>
#include <vector>

namespace rs {

std::optional<ParsedFile> parse_file(const std::filesystem::path& path) {
    std::string content;
    if (!read_file(path, content))
        return std::nullopt;

    return build_parsed_file(path, std::move(content));
}

std::vector<ParsedFile> parse_directory(const std::filesystem::path& root) {
    std::vector<ParsedFile> results;
    auto paths = discover_source_files(root);

    results.reserve(paths.size());
    for (auto const& path : paths) {
        auto pf = parse_file(path);
        if (pf)
            results.push_back(std::move(*pf));
    }

    return results;
}

} // namespace rs
