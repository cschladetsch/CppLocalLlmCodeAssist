#include "cppcoder/FileRetriever.h"

#include "cppcoder/JsonUtil.h"
#include "cppcoder/LocalCommands.h"
#include "cppcoder/OllamaClient.h"
#include "cppcoder/ResearchEngine.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace cppcoder {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// Enough terms to catch a multi-part question without turning the grep
// into a full-tree read per keyword.
constexpr std::size_t kMaxKeywords = 8;
constexpr std::size_t kMaxHitsPerKeyword = 20;

// FallbackKeywords' stopword list is tuned for research questions
// ("how does the judge prune?"), not conversation, so ordinary chat
// words survive it. "you" is the worst offender: three letters, not a
// stopword, and present in a comment in nearly every file -- enough on
// its own to make "hello, how are you today?" look like a code question.
bool IsConversationalWord(const std::string& word) {
    static const std::vector<std::string> kWords = {
        "you",  "your", "yours", "me",    "my",   "mine",  "we",    "our",
        "ours", "they", "them",  "their", "hello","hi",    "hey",   "thanks",
        "thank","please","today","now",   "just", "really","about", "tell",
    };
    std::string lower = word;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return std::find(kWords.begin(), kWords.end(), lower) != kWords.end();
}

}  // namespace

std::vector<std::string> FindLikelyFiles(const std::string& userMessage,
                                          const CodebaseScanner& scanner,
                                          std::size_t maxResults) {
    const std::vector<std::string> keywords = FallbackKeywords(userMessage, kMaxKeywords);

    std::unordered_map<std::string, int> hitCounts;
    for (const auto& keyword : keywords) {
        if (IsConversationalWord(keyword)) continue;

        // Ask for one more than the cap purely to detect saturation: a
        // term that matches everything discriminates nothing, so it adds
        // noise to the shortlist rather than signal. Real identifiers
        // land in a handful of files.
        auto hits = scanner.FindFilesMatchingKeyword(keyword, kMaxHitsPerKeyword + 1);
        if (hits.size() > kMaxHitsPerKeyword) continue;

        for (const auto& file : hits) {
            ++hitCounts[file];
        }
    }

    std::vector<std::string> ranked;
    ranked.reserve(hitCounts.size());
    for (const auto& [file, _] : hitCounts) ranked.push_back(file);

    std::sort(ranked.begin(), ranked.end(),
              [&hitCounts](const std::string& a, const std::string& b) {
                  if (hitCounts[a] != hitCounts[b]) return hitCounts[a] > hitCounts[b];
                  return a < b;  // stable tiebreak so the prompt doesn't churn
              });

    if (ranked.size() > maxResults) ranked.resize(maxResults);
    return ranked;
}

std::string BuildRetrievalPrompt(const std::string& userMessage,
                                  const std::vector<std::string>& candidateFiles) {
    std::ostringstream prompt;
    prompt << "You are a retrieval planner for a coding assistant. Another model is about "
           << "to answer the user's message, and it can be given the contents of a few "
           << "files from this repository first.\n\n"
           << "Your only job is to choose which files it needs. Do not answer the message "
           << "yourself.\n\n"
           << "User message:\n-----\n"
           << userMessage << "\n-----\n\n"
           << "Candidate files, each of which contains a term from the message, most "
              "matches first:\n";
    for (const auto& f : candidateFiles) {
        prompt << "  " << f << "\n";
    }

    prompt << "\nRespond with ONLY a single JSON object, no prose, no markdown fences, "
              "matching this shape exactly:\n"
           << "{\n"
           << "  \"files\": [\"path/from/the/list/above.cpp\"]\n"
           << "}\n\n"
           << "Choose at most " << kMaxRetrievedFiles
           << " files, the ones whose contents are genuinely needed to answer well. "
           << "Copy paths exactly as they appear in the list above.\n"
           << "Return {\"files\": []} if the message is small talk, a general programming "
           << "question, or anything else that does not depend on this specific "
           << "repository's code. Choosing nothing is better than choosing a file that "
           << "only looks relevant.";
    return prompt.str();
}

std::vector<std::string> ParseFileRequests(const std::string& modelResponse) {
    std::vector<std::string> paths;

    std::string jsonStr = ExtractJsonObject(modelResponse);
    if (jsonStr.empty()) {
        spdlog::debug("FileRetriever: no JSON object in retrieval planner response");
        return paths;
    }

    try {
        json parsed = json::parse(jsonStr);
        if (!parsed.contains("files") || !parsed["files"].is_array()) return paths;

        for (const auto& entry : parsed["files"]) {
            if (!entry.is_string()) continue;
            std::string path = entry.get<std::string>();
            if (path.empty()) continue;
            paths.push_back(std::move(path));
        }
    } catch (const json::exception& e) {
        spdlog::debug("FileRetriever: malformed retrieval planner JSON: {}", e.what());
        return {};
    }
    return paths;
}

std::vector<RetrievedFile> ReadRequestedFiles(const std::vector<std::string>& paths,
                                               const fs::path& root) {
    std::vector<RetrievedFile> results;
    std::size_t totalBytes = 0;

    for (const auto& path : paths) {
        if (results.size() >= kMaxRetrievedFiles) break;

        RetrievedFile rf;
        rf.path = path;

        // Same guard a user's /read gets: the model is not more trusted
        // than the person typing, and it is the likelier source of a
        // bogus path since it may simply hallucinate one.
        auto resolved = ResolveWithinRoot(path, root);
        if (!resolved) {
            rf.error = "outside the accessible root";
            results.push_back(std::move(rf));
            continue;
        }

        std::error_code ec;
        if (!fs::is_regular_file(*resolved, ec)) {
            rf.error = "not a readable file";
            results.push_back(std::move(rf));
            continue;
        }

        std::ifstream in(*resolved, std::ios::binary);
        if (!in) {
            rf.error = "could not open";
            results.push_back(std::move(rf));
            continue;
        }

        std::ostringstream contentStream;
        contentStream << in.rdbuf();
        std::string content = contentStream.str();

        if (content.size() > kMaxBytesPerRetrievedFile) {
            content.resize(kMaxBytesPerRetrievedFile);
            rf.truncated = true;
        }
        // Stop before blowing the overall budget rather than after: a
        // partial file at the end of the context is worse than an
        // honest "not included".
        if (totalBytes + content.size() > kMaxTotalRetrievedBytes) {
            rf.error = "omitted -- context budget exhausted";
            results.push_back(std::move(rf));
            continue;
        }

        totalBytes += content.size();
        rf.content = std::move(content);
        rf.ok = true;
        results.push_back(std::move(rf));
    }

    return results;
}

std::string FormatFileContext(const std::vector<RetrievedFile>& files) {
    if (files.empty()) return {};

    std::ostringstream out;
    out << "Contents of files from the user's repository, retrieved to help you answer. "
        << "Treat these as the current source of truth.\n";

    bool anyContent = false;
    for (const auto& f : files) {
        if (!f.ok) continue;
        anyContent = true;
        out << "\n==== " << f.path << " ====\n" << f.content;
        if (!f.content.empty() && f.content.back() != '\n') out << "\n";
        if (f.truncated) out << "[truncated]\n";
    }

    std::ostringstream failures;
    for (const auto& f : files) {
        if (f.ok) continue;
        failures << "- " << f.path << ": " << f.error << "\n";
    }

    // Nothing was actually read -- returning the failure list alone would
    // spend context telling the model about paths it invented, so treat
    // it as "no context" and let the turn proceed normally.
    if (!anyContent) return {};

    if (failures.tellp() > 0) {
        out << "\nRequested but unavailable:\n" << failures.str();
    }
    return out.str();
}

std::string RunRetrievalPrePass(const std::string& userMessage, const std::string& model,
                                 const std::string& ollamaHost, int ollamaPort,
                                 const fs::path& root) {
    // "external" on top of the usual exclusions: vendored submodules are
    // 87% of this repo's source files by count, and a menu dominated by
    // googletest and spdlog headers both bloats the planner prompt and
    // makes the model's choice materially worse.
    CodebaseScanner scanner(root,
                             {".cpp", ".h", ".hpp", ".cc", ".cxx", ".py", ".rs", ".scala"},
                             {".git", "build", "external"});
    // Content grep first: paths alone are a weak signal for a small
    // model. If nothing in the tree mentions any term from the message,
    // the message isn't about this code -- return before spending a
    // round trip to have the model tell us the same thing. This is what
    // makes small talk cost nothing.
    const std::vector<std::string> candidates = FindLikelyFiles(userMessage, scanner);
    if (candidates.empty()) return {};

    OllamaConfig plannerConfig;
    plannerConfig.host = ollamaHost;
    plannerConfig.port = ollamaPort;
    plannerConfig.model = model;
    // Picking filenames off a list is a lookup, not a creative task, and
    // a wrong pick costs a wasted round trip plus wasted context.
    plannerConfig.temperature = 0.0;
    // Send no num_ctx, exactly like the streaming proxy that follows.
    // Ollama reloads the model whenever num_ctx changes, so asking for a
    // different window here would evict and reload the very model the
    // main turn is about to use -- twice per message. It also keeps the
    // pre-pass inside whatever VRAM the model already fits in.
    plannerConfig.numCtx = 0;
    // Deliberately shorter than the main turn's budget: the pre-pass is
    // an optimisation, and the user is waiting on it before any token of
    // the real answer streams back.
    plannerConfig.timeoutSeconds = 60;

    OllamaClient planner(plannerConfig);
    auto reply = planner.Generate(BuildRetrievalPrompt(userMessage, candidates));
    if (!reply) {
        // OllamaClient has already logged the specific cause (transport
        // error, HTTP status and body, or unparseable JSON), so don't
        // guess at one here.
        spdlog::warn("FileRetriever: retrieval pre-pass failed -- answering without file context");
        return {};
    }

    const std::vector<std::string> requested = ParseFileRequests(*reply);
    if (requested.empty()) return {};

    const std::vector<RetrievedFile> files = ReadRequestedFiles(requested, root);
    std::string context = FormatFileContext(files);
    if (context.empty()) {
        spdlog::info("FileRetriever: retrieval pre-pass requested {} file(s), none readable",
                     requested.size());
        return {};
    }

    for (const auto& f : files) {
        if (f.ok) {
            spdlog::info("FileRetriever: retrieved '{}'{}", f.path, f.truncated ? " (truncated)" : "");
        } else {
            spdlog::info("FileRetriever: skipped '{}' -- {}", f.path, f.error);
        }
    }
    return context;
}

}  // namespace cppcoder
