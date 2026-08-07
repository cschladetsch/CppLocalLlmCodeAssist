#include "cppcoder/LocalCommands.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace cppcoder {

namespace fs = std::filesystem;

fs::path FindRepoRoot(const fs::path& start) {
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(start, ec);
    if (ec) dir = start;

    while (!dir.empty()) {
        // exists() rather than is_directory(): a worktree/submodule
        // checkout has ".git" as a regular file holding a gitdir
        // pointer, and that is still a valid toplevel.
        if (fs::exists(dir / ".git", ec)) return dir;

        // Terminate on both shapes of "no parent left": POSIX "/" is its
        // own parent_path(), while Windows walks "C:/" -> "C:" -> "".
        // Without the empty check the last iteration would test ".git"
        // relative to the process cwd and wrongly return an empty root.
        fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return start;
}

namespace {

bool IsWithinRoot(const fs::path& resolved, const fs::path& root) {
    fs::path canonicalRoot = fs::weakly_canonical(root);
    fs::path canonicalResolved = fs::weakly_canonical(resolved);

    auto rootIt = canonicalRoot.begin();
    auto resolvedIt = canonicalResolved.begin();
    for (; rootIt != canonicalRoot.end(); ++rootIt, ++resolvedIt) {
        if (resolvedIt == canonicalResolved.end() || *resolvedIt != *rootIt) {
            return false;
        }
    }
    return true;
}

std::optional<fs::path> ResolveLocalPath(const std::string& userPath, const fs::path& root) {
    fs::path candidate(userPath);
    if (candidate.is_absolute()) return std::nullopt;

    fs::path resolved = root / candidate;
    if (!IsWithinRoot(resolved, root)) return std::nullopt;
    return resolved;
}

LocalCommandResult HandlePwd(const fs::path& root) {
    LocalCommandResult r;
    r.handled = true;
    r.text = root.string();
    return r;
}

LocalCommandResult HandleLs(const std::string& arg, const fs::path& root) {
    LocalCommandResult r;
    r.handled = true;

    fs::path target = root;
    if (!arg.empty()) {
        auto resolved = ResolveLocalPath(arg, root);
        if (!resolved) {
            r.text = "ls: '" + arg + "' is outside the accessible root";
            return r;
        }
        target = *resolved;
    }

    std::error_code ec;
    if (!fs::exists(target, ec) || !fs::is_directory(target, ec)) {
        r.text = "ls: '" + arg + "' is not a directory";
        return r;
    }

    std::vector<std::string> entries;
    for (auto it = fs::directory_iterator(target, fs::directory_options::skip_permission_denied, ec);
         it != fs::directory_iterator(); ++it) {
        if (ec) break;
        std::error_code entEc;
        std::string name = it->path().filename().string();
        if (it->is_directory(entEc)) name += "/";
        entries.push_back(std::move(name));
    }
    std::sort(entries.begin(), entries.end());

    std::ostringstream out;
    out << target.string() << ":\n";
    for (const auto& e : entries) out << "  " << e << "\n";
    if (entries.empty()) out << "  (empty)\n";
    r.text = out.str();
    return r;
}

constexpr std::size_t kMaxReadBytes = 200 * 1024;

LocalCommandResult HandleRead(const std::string& arg, const fs::path& root) {
    LocalCommandResult r;
    r.handled = true;
    if (arg.empty()) {
        r.text = "read: missing path (usage: /read <relative-path>)";
        return r;
    }
    auto resolved = ResolveLocalPath(arg, root);
    if (!resolved) {
        r.text = "read: '" + arg + "' is outside the accessible root";
        return r;
    }
    std::ifstream in(*resolved, std::ios::binary);
    if (!in) {
        r.text = "read: could not open '" + arg + "'";
        return r;
    }
    std::ostringstream contentStream;
    contentStream << in.rdbuf();
    std::string content = contentStream.str();
    bool truncated = content.size() > kMaxReadBytes;
    if (truncated) content.resize(kMaxReadBytes);

    std::ostringstream out;
    out << "--- " << arg << " ---\n" << content;
    if (truncated) out << "\n[truncated at " << kMaxReadBytes / 1024 << " KB]";
    r.text = out.str();
    return r;
}

LocalCommandResult HandleWrite(const std::string& arg, const std::string& content,
                                const fs::path& root) {
    LocalCommandResult r;
    r.handled = true;
    if (arg.empty()) {
        r.text = "write: missing path (usage: /write <relative-path>\\n<content>)";
        return r;
    }
    auto resolved = ResolveLocalPath(arg, root);
    if (!resolved) {
        r.text = "write: '" + arg + "' is outside the accessible root";
        return r;
    }

    std::error_code ec;
    fs::path parent = resolved->parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec) {
            r.text = "write: could not create directory for '" + arg + "': " + ec.message();
            return r;
        }
    }

    std::ofstream out(*resolved, std::ios::binary | std::ios::trunc);
    if (!out) {
        r.text = "write: could not open '" + arg + "' for writing";
        return r;
    }
    out << content;
    if (!out) {
        r.text = "write: write failed for '" + arg + "'";
        return r;
    }
    r.text = "write: wrote " + std::to_string(content.size()) + " byte(s) to '" + arg + "'";
    return r;
}

// Splits "/cmd rest" into {"/cmd", "rest"} on the first space or
// newline. The remainder is trimmed of leading spaces only -- /write
// needs its embedded newline (path\ncontent) intact.
std::pair<std::string, std::string> SplitCommand(const std::string& text) {
    std::size_t firstBreak = text.find_first_of(" \n");
    if (firstBreak == std::string::npos) return {text, ""};
    std::string cmd = text.substr(0, firstBreak);
    std::string rest = text.substr(firstBreak + 1);
    std::size_t start = rest.find_first_not_of(' ');
    rest = start == std::string::npos ? "" : rest.substr(start);
    return {cmd, rest};
}

}  // namespace

LocalCommandResult TryHandleLocalCommand(const std::string& message, const fs::path& root) {
    auto [cmd, rest] = SplitCommand(message);
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "/pwd" || cmd == "/cwd") return HandlePwd(root);
    if (cmd == "/ls") return HandleLs(rest, root);
    if (cmd == "/read" || cmd == "/cat") return HandleRead(rest, root);
    if (cmd == "/write") {
        std::size_t nl = rest.find('\n');
        std::string path = nl == std::string::npos ? rest : rest.substr(0, nl);
        std::string content = nl == std::string::npos ? "" : rest.substr(nl + 1);
        return HandleWrite(path, content, root);
    }
    return {};
}

}  // namespace cppcoder
