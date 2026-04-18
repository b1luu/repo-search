#include "python_modules.h"

#include "source_path.h"

#include <filesystem>

namespace rs {

namespace fs = std::filesystem;

static std::string lowercase_copy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    }
    return s;
}

// Compute the longest common directory prefix across all indexed Python paths.
static fs::path compute_corpus_root(const std::vector<std::string>& paths) {
    if (paths.empty())
        return {};

    fs::path root = fs::path(paths[0]).parent_path();
    bool found_python = false;

    for (auto const& raw_path : paths) {
        const fs::path path(raw_path);
        if (classify_source_path(path) != SourceLang::python)
            continue;

        const fs::path dir = normalize_source_path(path.parent_path());
        if (!found_python) {
            root = dir;
            found_python = true;
            continue;
        }

        fs::path common;
        auto r = root.begin();
        auto d = dir.begin();
        while (r != root.end() && d != dir.end() && *r == *d) {
            common /= *r;
            ++r;
            ++d;
        }
        root = common;
    }

    return found_python ? root : fs::path{};
}

// Convert a Python file path to its dotted module path relative to `root`.
static std::string file_to_python_module(const fs::path& file, const fs::path& root) {
    const fs::path rel = normalize_source_path(file).lexically_relative(root);
    if (rel.empty() || *rel.begin() == "..")
        return {};

    std::string module;
    for (auto it = rel.begin(); it != rel.end(); ++it) {
        auto next = it;
        ++next;
        if (next == rel.end()) {
            const std::string stem = rel.stem().string();
            if (stem == "__init__")
                break;
            if (!module.empty())
                module += '.';
            module += stem;
        } else {
            if (!module.empty())
                module += '.';
            module += it->string();
        }
    }

    return module;
}

static std::string resolve_python_relative(std::string_view import_str, std::string_view file_module,
                                           bool is_init) {
    std::size_t dots = 0;
    while (dots < import_str.size() && import_str[dots] == '.')
        ++dots;

    std::string package(file_module);
    if (!is_init) {
        const auto pos = package.rfind('.');
        package = (pos != std::string::npos) ? package.substr(0, pos) : "";
    }

    for (std::size_t i = 1; i < dots; ++i) {
        const auto pos = package.rfind('.');
        package = (pos != std::string::npos) ? package.substr(0, pos) : "";
    }

    const std::string_view rest = import_str.substr(dots);
    if (rest.empty())
        return package;
    if (package.empty())
        return std::string(rest);
    return package + "." + std::string(rest);
}

PythonModuleMap PythonModuleMap::build(const Index& idx) {
    PythonModuleMap map;
    map.file_module.resize(idx.num_docs);
    map.file_is_init.assign(idx.num_docs, false);
    map.module_to_id.reserve(idx.num_docs);

    const fs::path corpus_root = compute_corpus_root(idx.paths);

    for (uint32_t fid = 0; fid < idx.num_docs; ++fid) {
        const fs::path path(idx.paths[fid]);
        if (classify_source_path(path) != SourceLang::python)
            continue;

        std::string module = lowercase_copy(file_to_python_module(path, corpus_root));
        if (!module.empty())
            map.module_to_id.try_emplace(module, fid);
        map.file_module[fid] = std::move(module);
        map.file_is_init[fid] = (path.stem() == "__init__");
    }

    return map;
}

std::optional<uint32_t> PythonModuleMap::resolve_import(uint32_t file_id,
                                                        std::string_view import_str) const {
    if (file_id >= file_module.size() || import_str.empty())
        return std::nullopt;

    std::string resolved;
    if (import_str.front() == '.') {
        resolved = lowercase_copy(
            resolve_python_relative(import_str, file_module[file_id], file_is_init[file_id]));
    } else {
        resolved = lowercase_copy(std::string(import_str));
    }

    if (resolved.empty())
        return std::nullopt;

    const auto it = module_to_id.find(resolved);
    if (it == module_to_id.end())
        return std::nullopt;
    return it->second;
}

} // namespace rs
