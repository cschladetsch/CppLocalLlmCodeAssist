#pragma once

#include "cppcoder/MemoryStore.h"
#include "cppcoder/OllamaClient.h"

#include <string>

namespace cppcoder {

struct ChatServerConfig {
    // Address/port the chat web UI + API are served on.
    std::string bindHost = "127.0.0.1";
    int bindPort = 8765;

    // Ollama connection used to service /api/models and /api/chat.
    // "127.0.0.1" not "127.0.0.1" -- see OllamaClient.h for why.
    std::string ollamaHost = "127.0.0.1";
    int ollamaPort = 11434;
    std::string defaultModel = kDefaultOllamaModel;

    // Directory containing chat.html (and friends) to serve as static
    // files at "/". Resolved by main.cpp before construction.
    std::string webRoot;

    // Path to the persisted facts file (see MemoryStore). Empty means
    // "use MemoryStore::ResolveDefaultPath()".
    std::string memoryFilePath;

    // Root local filesystem commands and retrieval are allowed to read
    // and write under. Empty means "nearest enclosing git repository,
    // falling back to the process cwd".
    std::string fileRoot;

    // Whether to run the retrieval pre-pass (FileRetriever.h) that lets
    // the assistant read repository files before answering. Costs one
    // extra non-streaming model round trip per chat turn, so it's worth
    // turning off for pure conversational use on slow hardware.
    bool fileContextEnabled = true;

    // Whether to open the chat UI in the default browser once the server
    // is ready to accept connections.
    bool openBrowser = true;
};

// Minimal local web server backing the "Claude for Desktop"-style chat
// UI (web/chat.html): serves the static frontend and proxies chat turns
// straight through to Ollama's /api/chat (no research engine involved --
// this is plain conversational chat with swappable models).
//
// Also owns a MemoryStore: every user message is scanned for durable
// facts (name, age, ...), which get persisted and re-injected as a
// system message on every subsequent turn, so the assistant "remembers"
// them across conversations and model switches.
class ChatServer {
public:
    explicit ChatServer(ChatServerConfig config);

    // Blocks the calling thread serving requests until the process is
    // interrupted (Ctrl+C). Returns a process exit code.
    int Run();

private:
    ChatServerConfig config_;
    MemoryStore memory_;
};

}  // namespace cppcoder

