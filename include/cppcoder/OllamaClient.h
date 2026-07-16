#pragma once

#include <optional>
#include <string>
#include <vector>

namespace cppcoder {

struct OllamaConfig {
    std::string host = "localhost";
    int port = 11434;
    std::string model = "qwen2.5-coder:7b";
    double temperature = 0.2;
    int numCtx = 32768;  // context window requested from Ollama
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

    // Returns the tags of every model Ollama currently has pulled locally
    // (via /api/tags). Returns an empty vector if Ollama is unreachable.
    std::vector<std::string> ListModels() const;

    const OllamaConfig& config() const { return config_; }

private:
    OllamaConfig config_;
};

}  // namespace cppcoder
