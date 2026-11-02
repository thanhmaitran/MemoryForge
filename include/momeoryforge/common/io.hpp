#pragma once

#include <filesystem>
#include <string>

#include "memoryforge/common/result.hpp"

namespace memoryforge {

Result<std::string> read_text_file(const std::filesystem::path& path);

/// Creates parent directories as needed, then writes `content` (binary mode, no newline translation).
Status write_text_file(const std::filesystem::path& path, const std::string& content);

}  // namespace memoryforge
