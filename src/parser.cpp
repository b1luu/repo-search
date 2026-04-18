#include "parser.h"

#include "import_extractor.h"
#include "source_discovery.h"
#include "source_path.h"
#include "tokenizer.h"

#include <fstream>
#include <optional>
#include <unordered_set>
#include <vector>

namespace rs {

// Deduplicate `v` in place, preserving the first-occurrence order.
static void dedupe_preserve_order(std::vector<std::string>& v) {
    if (v.size() < 2)
        return;
    std::unordered_set<std::string> seen;
    std::vector<std::string> out;
    seen.reserve(v.size());
    out.reserve(v.size());
    for (auto& s : v) {
        if (seen.insert(s).second) {
            out.push_back(std::move(s));
        }
    }
    v = std::move(out);
}

// ---------------------------------------------------------------------------
// File reading
// ---------------------------------------------------------------------------

// Read entire file into a std::string.  Returns false on failure.
static bool read_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;

    f.seekg(0, std::ios::end);
    const auto size = f.tellg();
    if (size <= 0) {
        out.clear();
        return true; // empty file is valid
    }
    out.resize(static_cast<std::size_t>(size));
    f.seekg(0);
    f.read(out.data(), size);
    return f.good() || f.eof();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

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
