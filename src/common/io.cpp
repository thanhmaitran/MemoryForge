#include "memoryforge/common/io.hpp"

#include <fstream>
#include <sstream>

namespace memoryforge {

Result<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return make_error(ErrorCode::IoError, "cannot open '" + path.string() + "' for reading");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) return make_error(ErrorCode::IoError, "failed while reading '" + path.string() + "'");
    return buffer.str();
}

Status write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return make_error(ErrorCode::IoError,
                          "cannot create directory '" + path.parent_path().string() + "': " + ec.message());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return make_error(ErrorCode::IoError, "cannot open '" + path.string() + "' for writing");
    out << content;
    out.flush();
    if (!out) return make_error(ErrorCode::IoError, "failed while writing '" + path.string() + "'");
    return Status::success();
}

}  // namespace memoryforge
