#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rs {

// Parsed representation of a single source file.
// `content` owns the raw file bytes (lowercased in-place during tokenization).
// `token_views` are views into `content` — do not move/copy `content` after parsing.
struct ParsedFile {
    std::string path;                          // canonical path as string
    std::string content;                       // raw file bytes (mutated: lowercased)
    std::vector<std::string_view> token_views; // views into content
    std::vector<std::string> imports;          // resolved module names from import stmts
};

// Parse a single supported source file. Returns nullopt on I/O error.
// Extracts tokens via tokenizer and (for Python files) import names.
std::optional<ParsedFile> parse_file(const std::filesystem::path& path);

// Recursively walk `root`, parse supported source files, and return results.
// Files that fail to open are silently skipped.
std::vector<ParsedFile> parse_directory(const std::filesystem::path& root);

} // namespace rs
