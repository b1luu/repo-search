#include "file_reader.h"

#include <fstream>

namespace rs {

bool read_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;

    f.seekg(0, std::ios::end);
    const auto size = f.tellg();
    if (size <= 0) {
        out.clear();
        return true;
    }
    out.resize(static_cast<std::size_t>(size));
    f.seekg(0);
    f.read(out.data(), size);
    return f.good() || f.eof();
}

} // namespace rs
