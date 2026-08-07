#include "cppcoder/CodebaseScanner.h"
#include "cppcoder/Types.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace cppcoder {

namespace {

// Relative paths from fs::relative() use native separators (backslash
// on Windows). Every consumer of these strings -- tests, the JSONL event
// schema, the web UI, target_area matching against Task areas -- expects
// forward-slash paths. std::filesystem::path::generic_string() is the
// standard, portable way to get that regardless of platform.
std::string ToPortablePath(const fs::path& p) { return p.generic_string(); }

}  // namespace

CodebaseScanner::CodebaseScanner(fs::path root, std::vector<std::string> extensions,
                                  std::vector<std::string> excludedDirs)
    : root_(std::move(root)),
      extensions_(std::move(extensions)),
      excludedDirs_(std::move(excludedDirs)) {}

bool CodebaseScanner::HasTrackedExtension(const fs::path& p) const {
    const std::string ext = p.extension().string();
    return std::find(extensions_.begin(), extensions_.end(), ext) != extensions_.end();
}

// Matches against the path *relative to root_*, not the absolute path.
// Checking the absolute path would exclude everything whenever the root
// itself sits under a directory with an excluded name -- scanning a
// checkout that lives in ".../external/spdlog" or in a folder called
// "build" would silently yield zero files.
//
// fs::path iteration splits on the platform's native separator
// automatically, so this works whether the path uses '/' (Linux/macOS)
// or '\' (Windows) -- unlike a substring search for "/.git/", which
// silently never matches on Windows and would let excluded directories
// through unfiltered.
bool CodebaseScanner::IsExcluded(const fs::path& absolutePath) const {
    std::error_code ec;
    fs::path rel = fs::relative(absolutePath, root_, ec);
    // A failed relative() means the path isn't under root_ at all; fall
    // back to the absolute path rather than silently letting it through.
    const fs::path& toCheck = ec || rel.empty() ? absolutePath : rel;

    for (const auto& part : toCheck) {
        const std::string name = part.string();
        if (std::find(excludedDirs_.begin(), excludedDirs_.end(), name) != excludedDirs_.end()) {
            return true;
        }
    }
    return false;
}

ScanResult CodebaseScanner::Scan(const std::string& targetArea, std::size_t tokenBudget) const {
    ScanResult result;
    fs::path start = targetArea.empty() ? root_ : root_ / targetArea;

    std::vector<fs::path> candidates;
    std::error_code ec;
    if (fs::is_regular_file(start, ec)) {
        candidates.push_back(start);
    } else if (fs::is_directory(start, ec)) {
        for (auto it = fs::recursive_directory_iterator(
                 start, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); ++it) {
            if (ec) break;
            const auto& entry = *it;
            if (IsExcluded(entry.path())) continue;
            if (entry.is_regular_file(ec) && HasTrackedExtension(entry.path())) {
                candidates.push_back(entry.path());
            }
        }
    }
    std::sort(candidates.begin(), candidates.end());

    std::ostringstream out;
    std::size_t tokensUsed = 0;
    for (const auto& file : candidates) {
        std::ifstream in(file, std::ios::binary);
        if (!in) continue;
        std::ostringstream contentStream;
        contentStream << in.rdbuf();
        std::string content = contentStream.str();

        std::string relPath = ToPortablePath(fs::relative(file, root_, ec));
        std::string header = "\n// ==== " + relPath + " ====\n";
        std::size_t entryTokens = EstimateTokens(header) + EstimateTokens(content);

        if (tokensUsed + entryTokens > tokenBudget) {
            result.filesSkippedBudget.push_back(relPath);
            continue;
        }

        out << header << content;
        tokensUsed += entryTokens;
        result.filesIncluded.push_back(relPath);
    }

    result.concatenatedContent = out.str();
    result.approxTokens = tokensUsed;
    return result;
}

std::vector<std::string> CodebaseScanner::ListFiles(std::size_t maxResults) const {
    std::vector<std::string> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             root_, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec) break;
        const auto& entry = *it;
        if (IsExcluded(entry.path())) continue;
        if (!entry.is_regular_file(ec) || !HasTrackedExtension(entry.path())) continue;
        files.push_back(ToPortablePath(fs::relative(entry.path(), root_, ec)));
    }
    // Sort before truncating so the cap takes a stable prefix rather than
    // whatever order the directory iterator happened to yield.
    std::sort(files.begin(), files.end());
    if (files.size() > maxResults) files.resize(maxResults);
    return files;
}

std::vector<std::string> CodebaseScanner::FindFilesMatchingKeyword(const std::string& keyword,
                                                                     std::size_t maxResults) const {
    std::vector<std::string> matches;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             root_, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec) break;
        const auto& entry = *it;
        if (IsExcluded(entry.path())) continue;
        if (!entry.is_regular_file(ec) || !HasTrackedExtension(entry.path())) continue;

        std::ifstream in(entry.path(), std::ios::binary);
        if (!in) continue;
        std::ostringstream contentStream;
        contentStream << in.rdbuf();
        std::string content = contentStream.str();

        auto lowerFind = [](std::string haystack, std::string needle) {
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
            std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            return haystack.find(needle) != std::string::npos;
        };

        if (lowerFind(content, keyword) || lowerFind(entry.path().filename().string(), keyword)) {
            matches.push_back(ToPortablePath(fs::relative(entry.path(), root_, ec)));
            if (matches.size() >= maxResults) break;
        }
    }
    return matches;
}

}  // namespace cppcoder
