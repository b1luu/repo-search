#pragma once

#include "search_engine.h"

#include <filesystem>
#include <string>

namespace rs {

struct CliOptions {
    SearchParams params{};
    std::filesystem::path root;
    std::string query;
};

enum class CliParseResult { Ok, Help, Error };

// Parse argv into CliOptions. Prints usage/errors to stderr.
// Returns Help when --help/-h is requested, Error on invalid input, Ok on success.
CliParseResult parse_cli(int argc, char* argv[], CliOptions& out);

void print_usage(const char* prog);

} // namespace rs
