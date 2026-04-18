#pragma once

#include "parser.h"

#include <filesystem>
#include <string>

namespace rs {

// Build a ParsedFile from already-read file contents.
ParsedFile build_parsed_file(const std::filesystem::path& path, std::string content);

} // namespace rs
