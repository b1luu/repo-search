#include "js_modules.h"

#include "source_path.h"

#include <filesystem>
#include <string>

namespace rs {

namespace fs = std::filesystem;

static std::optional<uint32_t> resolve_js_relative_impl(
    const fs::path& from_file, std::string_view module_str,
    const std::unordered_map<std::string, uint32_t>& path_to_id) {
    if (module_str.empty() || module_str[0] != '.')
        return std::nullopt;

    const fs::path base = normalize_source_path(from_file.parent_path() / std::string(module_str));

    for (std::string_view ext : {".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs"}) {
        fs::path cand = base;
        cand += ext;
        const auto it = path_to_id.find(normalized_source_key(cand));
        if (it != path_to_id.end())
            return it->second;
    }

    for (std::string_view ext : {".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs"}) {
        const fs::path cand = base / (std::string("index") + std::string(ext));
        const auto it = path_to_id.find(normalized_source_key(cand));
        if (it != path_to_id.end())
            return it->second;
    }

    const auto it = path_to_id.find(normalized_source_key(base));
    if (it != path_to_id.end())
        return it->second;

    return std::nullopt;
}

JsModuleMap JsModuleMap::build(const Index& idx) {
    JsModuleMap map;
    map.path_to_id.reserve(idx.num_docs);

    for (uint32_t fid = 0; fid < idx.num_docs; ++fid) {
        const fs::path path(idx.paths[fid]);
        if (classify_source_path(path) == SourceLang::js_ts)
            map.path_to_id[normalized_source_key(path)] = fid;
    }

    return map;
}

std::optional<uint32_t> JsModuleMap::resolve_relative(uint32_t file_id, std::string_view module_str,
                                                      const Index& idx) const {
    if (file_id >= idx.paths.size())
        return std::nullopt;
    return resolve_js_relative_impl(fs::path(idx.paths[file_id]), module_str, path_to_id);
}

} // namespace rs
