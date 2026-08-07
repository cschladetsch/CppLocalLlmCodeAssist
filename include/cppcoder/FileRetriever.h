#pragma once

#include "cppcoder/CodebaseScanner.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace cppcoder {

// Chat mode's retrieval pre-pass: the step that lets the assistant answer
// questions about the codebase even though Ollama itself has no
// filesystem access.
//
// Before a chat turn is proxied to Ollama, ChatServer asks the model one
// cheap non-streaming question -- "which of these files do you need?" --
// reads whatever it names, and injects the contents as a system message.
// The streaming turn that follows is unchanged; the model just has the
// relevant source in front of it.
//
// This is deliberately a single retrieval step rather than a tool-call
// loop: it works with every model (no native tool support needed), keeps
// the /api/chat proxy a straight pass-through, and costs exactly one
// extra round trip. The tradeoff is that the model cannot read, think,
// and then read again within one turn.
//
// Everything here is a pure function apart from ReadRequestedFiles' own
// file I/O, so the parsing and formatting are testable without a running
// Ollama -- the same convention Worker::ParseWorkerResponse follows.

// Caps. A small local model's context is the real constraint, so the
// budget is deliberately far below what /read allows a user to pull in
// by hand.
inline constexpr std::size_t kMaxRetrievedFiles = 4;
inline constexpr std::size_t kMaxBytesPerRetrievedFile = 24 * 1024;
inline constexpr std::size_t kMaxTotalRetrievedBytes = 64 * 1024;

// Files whose *contents* mention identifier-like terms from the user's
// message, ranked by how many distinct terms each one matches (ties
// broken by path, so the order is stable).
//
// A filename-only menu badly misleads small models: asked what
// FindRepoRoot does, a 1.5b model picks CodebaseScanner.cpp because the
// name sounds right, and then confidently describes a function that
// isn't there. FindRepoRoot actually lives in LocalCommands.cpp, which
// no amount of path-guessing will surface -- but a content grep finds it
// immediately.
//
// Reuses ResearchEngine's FallbackKeywords for term extraction and the
// scanner's own keyword grep, so chat mode ranks candidates the same way
// research mode seeds its first task.
std::vector<std::string> FindLikelyFiles(const std::string& userMessage,
                                          const CodebaseScanner& scanner,
                                          std::size_t maxResults = 12);

// Builds the planner prompt from the user's message and the grep-ranked
// shortlist from FindLikelyFiles.
//
// Only the shortlist is offered, deliberately. Showing the model the
// whole repository as well made it pick a plausible-looking file for
// "hello, how are you today?" -- given a menu, a small model orders from
// it. Every candidate here is one that actually contains a term from the
// message, and an empty shortlist means the caller should skip the
// pre-pass rather than ask at all.
std::string BuildRetrievalPrompt(const std::string& userMessage,
                                  const std::vector<std::string>& candidateFiles);

// Pulls the requested paths out of the planner model's reply, which is
// asked for {"files": ["a/b.cpp", ...]} but in practice arrives wrapped
// in prose or markdown fences (hence ExtractJsonObject).
//
// Returns an empty vector for "no files needed", for a malformed reply,
// and for anything that isn't a JSON object with a string array at
// "files" -- a failed pre-pass degrades to a normal, contextless chat
// turn rather than an error.
std::vector<std::string> ParseFileRequests(const std::string& modelResponse);

struct RetrievedFile {
    std::string path;     // as requested, root-relative
    std::string content;  // empty when ok == false
    bool ok = false;
    bool truncated = false;
    std::string error;  // populated when ok == false
};

// Reads each requested path, confined to `root` by
// LocalCommands' ResolveWithinRoot -- a model's request is held to
// exactly the same boundary as a user's /read, so a hallucinated
// "../../.ssh/id_rsa" is refused rather than followed.
//
// Applies kMaxRetrievedFiles, kMaxBytesPerRetrievedFile and
// kMaxTotalRetrievedBytes. Unreadable or out-of-root paths come back as
// entries with ok == false rather than being dropped, so the caller can
// tell the model what it did not get.
std::vector<RetrievedFile> ReadRequestedFiles(const std::vector<std::string>& paths,
                                               const std::filesystem::path& root);

// Renders retrieved files as the system message injected ahead of the
// conversation. Returns an empty string if nothing was read, which the
// caller treats as "inject nothing".
std::string FormatFileContext(const std::vector<RetrievedFile>& files);

}  // namespace cppcoder
