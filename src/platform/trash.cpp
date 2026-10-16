#include "trash.h"

#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <system_error>

namespace driverse::platform {

namespace {

std::filesystem::path homeDir() {
    const char* home = std::getenv("HOME");
    return (home && *home) ? std::filesystem::path(home) : std::filesystem::path();
}

std::string deletionDate() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm);
    return std::string(buffer);
}

} // namespace

std::filesystem::path xdgTrashRoot() {
    if (const char* dataHome = std::getenv("XDG_DATA_HOME"); dataHome && *dataHome) {
        return std::filesystem::path(dataHome) / "Trash";
    }
    return homeDir() / ".local" / "share" / "Trash";
}

Trash::Trash(std::filesystem::path root)
    : m_root(root.empty() ? xdgTrashRoot() : std::move(root)) {}

bool Trash::ensureReady(std::string* error) const {
    std::error_code ec;
    std::filesystem::create_directories(m_root / "files", ec);
    if (ec) {
        if (error) {
            *error = "cannot create trash files dir " + (m_root / "files").string() +
                     ": " + ec.message();
        }
        return false;
    }
    std::filesystem::create_directories(m_root / "info", ec);
    if (ec) {
        if (error) {
            *error = "cannot create trash info dir " + (m_root / "info").string() +
                     ": " + ec.message();
        }
        return false;
    }
    return true;
}

std::filesystem::path Trash::uniqueName(const std::filesystem::path& base) const {
    const std::string name = base.string();
    std::error_code ec;
    std::filesystem::path candidate = base;
    for (int i = 1; i < 100000; ++i) {
        const bool fileTaken = std::filesystem::exists(m_root / "files" / candidate, ec);
        ec.clear();
        const bool infoTaken = std::filesystem::exists(
            m_root / "info" / (candidate.string() + ".trashinfo"), ec);
        ec.clear();
        if (!fileTaken && !infoTaken) {
            return candidate;
        }
        candidate = std::filesystem::path(name + "." + std::to_string(i));
    }
    return base;
}

std::string Trash::encodePath(const std::filesystem::path& path) {
    static const char* const kHex = "0123456789ABCDEF";
    const std::string in = path.string();
    std::string out;
    out.reserve(in.size());
    for (const char raw : in) {
        const auto c = static_cast<unsigned char>(raw);
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                                c == '.' || c == '~' || c == '/';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

TrashMoveResult Trash::move(const std::filesystem::path& path) const {
    TrashMoveResult result;

    if (path.empty()) {
        result.error = "empty path";
        return result;
    }

    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || status.type() == std::filesystem::file_type::not_found) {
        result.error = "path does not exist: " + path.string();
        return result;
    }

    const std::filesystem::path name = path.filename();
    if (name.empty() || name == "." || name == "..") {
        result.error = "refusing to trash a root directory: " + path.string();
        return result;
    }

    if (!ensureReady(&result.error)) {
        return result;
    }

    // Record the original absolute location before anything moves.
    std::error_code absEc;
    std::filesystem::path absolute = std::filesystem::absolute(path, absEc);
    if (absEc) {
        absolute = path;
    }
    absolute = absolute.lexically_normal();

    const std::filesystem::path dest = uniqueName(name);
    const std::filesystem::path destPath = m_root / "files" / dest;
    const std::filesystem::path infoPath = m_root / "info" / (dest.string() + ".trashinfo");

    // 1. Write the .trashinfo record first; nothing is moved until it exists.
    {
        std::ofstream info(infoPath, std::ios::trunc);
        if (!info) {
            result.error = "cannot write trash info file " + infoPath.string();
            return result;
        }
        info << "[Trash Info]\n"
             << "Path=" << encodePath(absolute) << "\n"
             << "DeletionDate=" << deletionDate() << "\n";
        info.flush();
        if (!info) {
            info.close();
            std::error_code removeEc;
            std::filesystem::remove(infoPath, removeEc);
            result.error = "failed writing trash info file " + infoPath.string();
            return result;
        }
    }

    auto dropInfoRecord = [&infoPath]() {
        std::error_code removeEc;
        std::filesystem::remove(infoPath, removeEc);
    };

    // 2. Move into the trash. rename() does not follow symlinks.
    std::filesystem::rename(path, destPath, ec);
    if (ec) {
        if (ec.value() == EXDEV) {
            // Different filesystem: copy the tree (keeping symlinks as links),
            // then remove the original. The copy stays recoverable.
            std::error_code copyEc;
            const auto copyOptions = std::filesystem::copy_options::recursive |
                                     std::filesystem::copy_options::copy_symlinks;
            std::filesystem::copy(path, destPath, copyOptions, copyEc);
            if (copyEc) {
                result.error = "copy to trash failed: " + copyEc.message();
                std::error_code cleanupEc;
                std::filesystem::remove_all(destPath, cleanupEc);
                dropInfoRecord();
                return result;
            }
            std::error_code removeEc;
            std::filesystem::remove_all(path, removeEc);
            if (removeEc) {
                // The copy exists; keep the record so it can be restored.
                result.error = "copied to trash but could not remove the original: " +
                               removeEc.message();
                return result;
            }
        } else {
            result.error = "rename to trash failed: " + ec.message();
            dropInfoRecord();
            return result;
        }
    }

    result.ok = true;
    result.trashedPath = destPath;
    return result;
}

} // namespace driverse::platform
