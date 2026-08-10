#pragma once

// Shared scaffolding for the *live* Ollama integration tests
// (tests/*LiveTests.cpp, built into the separate cppcoder_ollama_tests
// target -- see tests/CMakeLists.txt). Every other test in this repo is
// a pure function exercised without any network access (see the
// top-level README's testing section); these are the deliberate
// exception; and unlike the pure suite, they need a real Ollama
// instance with the default model pulled, so every test here must call
// SkipUnlessOllamaReady() first and skip (not fail) when that's not the
// case -- a machine without Ollama running should never see a red X.

#include "cppcoder/OllamaClient.h"

#include <gtest/gtest.h>

namespace cppcoder_test {

inline cppcoder::OllamaConfig LiveOllamaConfig() {
    cppcoder::OllamaConfig config;
    config.model = cppcoder::kDefaultOllamaModel;
    return config;
}

// Call at the top of every live-model TEST/TEST_F body, before doing
// anything else. GTEST_SKIP() unwinds the current test function (it's a
// macro that `return`s), so callers don't need to check a return value
// -- just call this and continue writing the test as if Ollama were
// always there.
inline void SkipUnlessOllamaReady(const cppcoder::OllamaConfig& config = LiveOllamaConfig()) {
    cppcoder::OllamaClient client(config);
    if (!client.IsServerReachable()) {
        GTEST_SKIP() << "Ollama not reachable at " << config.host << ":" << config.port
                     << " -- skipping live-model test.";
    }
    if (!client.IsModelAvailable()) {
        GTEST_SKIP() << "Model '" << config.model << "' is not pulled -- skipping live-model test.";
    }
}

}  // namespace cppcoder_test
