#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace rs {

enum class SourceLang {
    unsupported,
    python,
    js_ts,
};

inline SourceLang classify_source_extension(std::string_view ext) {
    if (ext == ".py" || ext == ".pyi")
        return SourceLang::python;
    if (ext == ".ts" || ext == ".tsx" || ext == ".js" || ext == ".jsx" || ext == ".mjs" ||
        ext == ".cjs")
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
