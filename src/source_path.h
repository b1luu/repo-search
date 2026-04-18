#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace rs {

enum class SourceLang {
    unsupported,
    python,
    js_ts,
};

// Single source of truth for supported file extensions per language.
// Any code that probes or classifies extensions must consult these tables.
inline constexpr std::array<std::string_view, 2> kPythonExtensions = {".py", ".pyi"};
inline constexpr std::array<std::string_view, 6> kJsTsExtensions = {".ts",  ".tsx", ".js",
                                                                    ".jsx", ".mjs", ".cjs"};

inline SourceLang classify_source_extension(std::string_view ext) {
    for (auto e : kPythonExtensions)
        if (ext == e)
            return SourceLang::python;
    for (auto e : kJsTsExtensions)
        if (ext == e)
            return SourceLang::js_ts;
    return SourceLang::unsupported;
}

inline SourceLang classify_source_path(const std::filesystem::path& path) {
    return classify_source_extension(path.extension().string());
}

inline bool is_supported_source_path(const std::filesystem::path& path) {
    return classify_source_path(path) != SourceLang::unsupported;
}

inline std::filesystem::path normalize_source_path(const std::filesystem::path& path) {
    return path.lexically_normal();
}

inline std::string normalized_source_key(const std::filesystem::path& path) {
    return normalize_source_path(path).generic_string();
}

} // namespace rs
