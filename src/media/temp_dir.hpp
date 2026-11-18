#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace upscaler {

/// RAII wrapper around a temporary directory. Automatically removed on destruction.
class TempDir {
public:
    explicit TempDir(const std::string& prefix = "upscaler_") {
        auto tmp = std::filesystem::temp_directory_path() / (prefix + "XXXXXX");
        std::string tmpl = tmp.string();
        char* result = mkdtemp(tmpl.data());
        if (!result) {
            throw std::runtime_error("Failed to create temp directory");
        }
        m_path = result;
    }

    ~TempDir() {
        if (!m_path.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    TempDir(TempDir&& other) noexcept : m_path(std::move(other.m_path)) {
        other.m_path.clear();
    }

    TempDir& operator=(TempDir&& other) noexcept {
        if (this != &other) {
            if (!m_path.empty()) {
                std::error_code ec;
                std::filesystem::remove_all(m_path, ec);
            }
            m_path = std::move(other.m_path);
            other.m_path.clear();
        }
        return *this;
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

    /// Create a named subdirectory under this temp dir, returning its path.
    std::filesystem::path subdir(const std::string& name) {
        auto p = m_path / name;
        std::filesystem::create_directories(p);
        return p;
    }

private:
    std::filesystem::path m_path;
};

} // namespace upscaler
