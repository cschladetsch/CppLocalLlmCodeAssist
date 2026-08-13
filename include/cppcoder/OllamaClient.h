#pragma once

#include <optional>
#include <string>
#include <vector>

namespace cppcoder {

// Single source of truth for which model cppcoder/sarah talks to when
// the user hasn't passed --model. "qwen2.5-coder:1.5b" rather than the
// larger "qwen2.5-coder:7b": the 7b model's ~4.3GB CUDA buffer competes
// with whatever else is holding GPU memory (browser tabs, other apps'
// WDDM allocations) and can fail to load with a cudaMalloc OOM even when
// nvidia-smi reports enough free VRAM on paper. 1.5b fits comfortably.
inline constexpr const char* kDefaultOllamaModel = "qwen2.5-coder:1.5b";

struct OllamaConfig {
    // "127.0.0.1" rather than "127.0.0.1": on Windows, resolving
    // "127.0.0.1" can try the IPv6 (::1) address first and silently hang
    // for the full connection timeout before falling back to IPv4,
    // where Ollama actually listens by default.
    std::string host = "127.0.0.1";
    int port = 11434;
    std::string model = kDefaultOllamaModel;
    double temperature = 0.2;
    // Context window requested from Ollama. 0 means "don't send num_ctx
    // at all", letting Ollama use the model's own default -- which
    // matters because Ollama reloads a model whenever num_ctx changes,
    // so a caller that shares a model with the plain /api/chat proxy
    // (which sends no options) must send none either or every turn pays
    // for two reloads.
    int numCtx = 32768;
    int timeoutSeconds = 300;
};

// Thin synchronous wrapper around Ollama's /api/generate endpoint.
// One client per worker/judge call site; cheap to construct.
class OllamaClient {
public:
    explicit OllamaClient(OllamaConfig config);

    // Sends `prompt` (optionally with a system prompt) and returns the
    // raw text response. Returns std::nullopt on transport/HTTP failure.
    std::optional<std::string> Generate(const std::string& prompt,
                                         const std::string& systemPrompt = "") const;

    // Returns true if Ollama is reachable and reports the configured
    // model as available (via /api/tags).
    bool IsModelAvailable() const;

    // Returns true if anything is listening at host:port and answers
    // /api/tags, regardless of which models (if any) it has. Cheaper
    // than IsModelAvailable() when you just need a liveness check.
    bool IsServerReachable() const;

    // Returns the tags of every model Ollama currently has pulled locally
    // (via /api/tags). Returns an empty vector if Ollama is unreachable.
    std::vector<std::string> ListModels() const;

    const OllamaConfig& config() const { return config_; }

private:
    OllamaConfig config_;
};

}  // namespace cppcoder

