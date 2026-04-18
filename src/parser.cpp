#include "parser.h"

#include "collection_utils.h"
#include "file_reader.h"
#include "import_extractor.h"
#include "source_discovery.h"
#include "source_path.h"
#include "tokenizer.h"

#include <optional>
#include <vector>

namespace rs {

std::optional<ParsedFile> parse_file(const std::filesystem::path& path) {
    ParsedFile pf;
    pf.path = path.string();

    if (!read_file(path, pf.content))
        return std::nullopt;

    // Extract imports BEFORE tokenizing: the tokenizer lowercases content
    // in place, which would corrupt case-sensitive JS/TS module specifiers.
    const SourceLang lang = classify_source_path(path);
    extract_imports(lang, pf.content, pf.imports);
    dedupe_preserve_order(pf.imports);

    // Tokenize (mutates content to lowercase)
    pf.token_views = tokenize(pf.content);

    return pf;
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
