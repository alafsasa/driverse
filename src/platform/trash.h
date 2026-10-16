#pragma once

#include <filesystem>
#include <string>

namespace driverse::platform {

// Result of moving a single path into the trash.
struct TrashMoveResult {
    bool ok = false;
    std::filesystem::path trashedPath; // final location inside <root>/files
    std::string error;                 // empty on success
};

// XDG Trash root:
//   $XDG_DATA_HOME/Trash  else  $HOME/.local/share/Trash
// (freedesktop.org Trash specification layout).
std::filesystem::path xdgTrashRoot();

// Minimal, dependency-free implementation of the XDG Trash specification.
//
// A path is moved to <root>/files/<name> and a matching
// <root>/info/<name>.trashinfo record stores the original location so file
// managers can restore it. Cross-device moves fall back to copy + remove (the
// item is still recoverable). Symlinks are moved as links; their targets are
// never followed. The .trashinfo record is written *before* the move, so a
// partial failure can never leave a file in the trash with no record.
class Trash {
public:
    // `root` empty -> xdgTrashRoot().
    explicit Trash(std::filesystem::path root = {});

    const std::filesystem::path& root() const { return m_root; }

    // Create files/ and info/ if needed. Returns false and sets *error on I/O
    // failure (e.g. unwritable home directory).
    bool ensureReady(std::string* error = nullptr) const;

    // Move `path` into the trash. Never follows symlinks.
    TrashMoveResult move(const std::filesystem::path& path) const;

    // Percent-encode a path for the .trashinfo "Path=" field (RFC 2396 style).
    static std::string encodePath(const std::filesystem::path& path);

private:
    std::filesystem::path uniqueName(const std::filesystem::path& base) const;

    std::filesystem::path m_root;
};

} // namespace driverse::platform
