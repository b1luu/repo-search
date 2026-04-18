#pragma once

#include <filesystem>
#include <string>

namespace rs {

// Read an entire file into `out`. Returns false on open/read failure.
bool read_file(const std::filesystem::path& path, std::string& out);

} // namespace rs
