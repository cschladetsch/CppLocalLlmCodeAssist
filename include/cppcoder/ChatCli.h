#pragma once

#include "cppcoder/MemoryStore.h"

#include <string>

namespace cppcoder {

struct ChatCliConfig {
    // Ollama connection used to service the chat turn and the retrieval
    // pre-pass.
    std::string ollamaHost = "localhost";
    int ollamaPort = 11434;
    std::string model = "qwen2.5-coder:7b";

    // Path to the persisted facts file (see MemoryStore). Empty means
    // "use MemoryStore::ResolveDefaultPath()".
    std::string memoryFilePath;

    // Whether to run the retrieval pre-pass (FileRetriever.h) that lets
    // the assistant read repository files before answering. Costs one
    // extra non-streaming model round trip per turn, so it's worth
    // turning off for pure conversational use on slow hardware.
    bool fileContextEnabled = true;
};

// Terminal counterpart to ChatServer: the same Ollama-backed chat turn
// (retrieval pre-pass, remembered facts, /pwd /ls /read /write local
// commands) but read from stdin and streamed to stdout instead of served
// over HTTP -- a "Claude Code"-style interactive REPL against a local
// model. Started with `cppcoder --cli`.
class ChatCli {
public:
    explicit ChatCli(ChatCliConfig config);

    // Runs the read-eval-print loop until EOF (Ctrl+D / Ctrl+Z) or the
    // user types /exit or /quit. Returns a process exit code.
    int Run();

private:
    // Streams one /api/chat turn from Ollama straight to stdout as it
    // arrives and returns the full assistant reply, or an empty string
    // on transport/HTTP failure (after printing the error to stderr).
    // `messagesJson` is a serialized JSON array of {"role","content"}
    // objects, mirroring the body ChatServer forwards to Ollama.
    std::string StreamChat(const std::string& messagesJson) const;

    ChatCliConfig config_;
    MemoryStore memory_;
};

}  // namespace cppcoder
