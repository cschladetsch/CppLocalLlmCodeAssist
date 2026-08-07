#pragma once

#include <filesystem>
#include <string>

namespace cppcoder {

// Walks up from `start` looking for a ".git" entry and returns the
// directory containing it -- the repository's toplevel, the same
// directory `git rev-parse --show-toplevel` reports. Matches both a
// ".git" directory (ordinary clone) and a ".git" file (worktree or
// submodule, where it holds a "gitdir:" pointer).
//
// The nearest enclosing repository wins, so running inside a submodule
// scopes to that submodule rather than the outer superproject.
//
// Returns `start` unchanged if no ".git" is found before hitting the
// filesystem root, which keeps callers usable outside a repository.
std::filesystem::path FindRepoRoot(const std::filesystem::path& start);

struct LocalCommandResult {
    bool handled = false;  // false: not a local command, fall through to Ollama
    std::string text;      // reply text (result on success, error message on failure)
};

// Recognizes and executes ChatServer's local filesystem chat commands
// -- /pwd, /cwd, /ls [path], /read <path> (alias /cat), and
// /write <path>\n<content> -- against `root`, rejecting any path
// argument that resolves outside it (same root-confinement guard as
// PatchApplier::IsPathSafe, just scoped to a caller-supplied root
// instead of a codebase root).
//
// Pure aside from the filesystem I/O the commands themselves perform;
// takes `root` explicitly (rather than deriving one itself) so tests
// can point it at a temp directory. ChatServer.cpp calls this with
// FindRepoRoot(std::filesystem::current_path()), so the reachable area
// is the whole checkout rather than just the directory the server
// happened to be launched from.
//
// Returns handled=false for any message that isn't one of these
// commands, leaving the caller to fall through to its normal path.
LocalCommandResult TryHandleLocalCommand(const std::string& message,
                                          const std::filesystem::path& root);

}  // namespace cppcoder
