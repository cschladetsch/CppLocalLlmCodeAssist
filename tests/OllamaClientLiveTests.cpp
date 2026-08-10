// Live integration tests for cppcoder::OllamaClient. Unlike every other
// test in this repo, these hit a *real*, currently-running local Ollama
// instance over the network (see tests/OllamaTestSupport.h for the
// rationale and the shared skip-if-unavailable scaffolding). Every test
// here calls cppcoder_test::SkipUnlessOllamaReady() first so a machine
// without Ollama running sees clean skips, not failures.
//
// Because a small local LLM's exact wording is non-deterministic even at
// low temperature, these tests never assert on exact response text --
// only on structural/behavioral invariants (has_value(), non-empty,
// specific known-pulled tags, reachability booleans, etc).

#include "cppcoder/OllamaClient.h"
#include "OllamaTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

using cppcoder::OllamaClient;
using cppcoder::OllamaConfig;
using cppcoder::kDefaultOllamaModel;

namespace {

// Config pointing at a definitely-closed port on localhost, with a short
// timeout so unreachable-host tests fail fast instead of hanging. Note:
// OllamaClient hardcodes its httplib connection timeout (2-10s depending
// on the method) rather than deriving it from OllamaConfig::timeoutSeconds
// -- that field only feeds Generate()'s read/write timeout -- but a
// connection attempt to a closed TCP port fails almost immediately with
// "connection refused" regardless, so these tests are fast either way.
// We still set timeoutSeconds short here to keep intent obvious and to
// avoid ever depending on the read-timeout path.
OllamaConfig UnreachableConfig() {
    OllamaConfig config;
    config.host = "127.0.0.1";
    config.port = 1;  // Reserved port, nothing listens here.
    config.model = kDefaultOllamaModel;
    config.timeoutSeconds = 2;
    return config;
}

}  // namespace

TEST(OllamaClientLiveTest, IsServerReachableTrueForRealHost) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    EXPECT_TRUE(client.IsServerReachable());
}

TEST(OllamaClientLiveTest, IsModelAvailableTrueForPulledModel) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    EXPECT_TRUE(client.IsModelAvailable());
}

TEST(OllamaClientLiveTest, IsModelAvailableFalseForNonexistentModel) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaConfig config = cppcoder_test::LiveOllamaConfig();
    config.model = "definitely-not-a-real-model:999b";
    OllamaClient client(config);

    // Server is reachable, but the bogus model tag is not in the pulled
    // list.
    EXPECT_TRUE(client.IsServerReachable());
    EXPECT_FALSE(client.IsModelAvailable());
}

TEST(OllamaClientLiveTest, ListModelsNonEmptyContainsExpectedSubstring) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    std::vector<std::string> models = client.ListModels();

    ASSERT_FALSE(models.empty());
    bool foundQwenCoder = std::any_of(models.begin(), models.end(), [](const std::string& name) {
        return name.find("qwen2.5-coder") != std::string::npos;
    });
    EXPECT_TRUE(foundQwenCoder);
}

TEST(OllamaClientLiveTest, ListModelsContainsConfiguredModelExactly) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    std::vector<std::string> models = client.ListModels();

    EXPECT_NE(std::find(models.begin(), models.end(), std::string(kDefaultOllamaModel)), models.end());
}

TEST(OllamaClientLiveTest, GenerateShortPromptNoSystemPromptReturnsContent) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    std::optional<std::string> result = client.Generate("Reply with the single word OK.");

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());
}

TEST(OllamaClientLiveTest, GenerateWithSystemAndUserPromptReturnsContent) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    std::optional<std::string> result =
        client.Generate("What is 2+2? Answer with just the number.",
                         "You are a terse assistant that answers in as few words as possible.");

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());
}

TEST(OllamaClientLiveTest, GenerateEmptyPromptBehavesAsExpected) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    std::optional<std::string> result = client.Generate("");

    // Verified against the real server: Ollama's /api/generate answers an
    // empty prompt with HTTP 200 and an empty "response" field (rather
    // than an error), so OllamaClient::Generate returns a
    // non-nullopt-but-empty string, not std::nullopt.
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST(OllamaClientLiveTest, TwoIndependentClientsSameConfigBothWork) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaConfig config = cppcoder_test::LiveOllamaConfig();
    OllamaClient clientA(config);
    OllamaClient clientB(config);

    EXPECT_TRUE(clientA.IsServerReachable());
    EXPECT_TRUE(clientB.IsServerReachable());

    std::optional<std::string> resultA = clientA.Generate("Reply with the single word OK.");
    std::optional<std::string> resultB = clientB.Generate("Reply with the single word OK.");

    ASSERT_TRUE(resultA.has_value());
    EXPECT_FALSE(resultA->empty());
    ASSERT_TRUE(resultB.has_value());
    EXPECT_FALSE(resultB->empty());
}

TEST(OllamaClientLiveTest, UnreachablePortIsServerReachableFalse) {
    // No SkipUnlessOllamaReady() here: this test is specifically about a
    // client pointed at a closed port, independent of whether the real
    // Ollama instance elsewhere is up.
    OllamaClient client(UnreachableConfig());
    EXPECT_FALSE(client.IsServerReachable());
}

TEST(OllamaClientLiveTest, UnreachablePortIsModelAvailableFalse) {
    OllamaClient client(UnreachableConfig());
    EXPECT_FALSE(client.IsModelAvailable());
}

TEST(OllamaClientLiveTest, UnreachablePortListModelsEmpty) {
    OllamaClient client(UnreachableConfig());
    EXPECT_TRUE(client.ListModels().empty());
}

TEST(OllamaClientLiveTest, UnreachableHostGenerateReturnsNulloptQuickly) {
    OllamaClient client(UnreachableConfig());

    auto start = std::chrono::steady_clock::now();
    std::optional<std::string> result = client.Generate("Reply with the single word OK.");
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.has_value());
    // Connection to a closed TCP port fails almost immediately; give a
    // generous ceiling well under Generate()'s hardcoded 10s connection
    // timeout to guard against this test silently regressing into a slow
    // hang.
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 10);
}

TEST(OllamaClientLiveTest, ConfigRoundTripsConstructorArguments) {
    OllamaConfig config;
    config.host = "127.0.0.1";
    config.port = 11434;
    config.model = kDefaultOllamaModel;
    config.temperature = 0.2;
    config.numCtx = 32768;

    OllamaClient client(config);
    const OllamaConfig& roundTripped = client.config();

    EXPECT_EQ(roundTripped.host, config.host);
    EXPECT_EQ(roundTripped.port, config.port);
    EXPECT_EQ(roundTripped.model, config.model);
    EXPECT_DOUBLE_EQ(roundTripped.temperature, config.temperature);
    EXPECT_EQ(roundTripped.numCtx, config.numCtx);
}

TEST(OllamaClientLiveTest, ConfigReflectsCustomTemperatureAndNumCtxWithoutLiveCall) {
    // No network call needed -- config() is a pure accessor.
    OllamaConfig config;
    config.temperature = 0.95;
    config.numCtx = 4096;

    OllamaClient client(config);

    EXPECT_DOUBLE_EQ(client.config().temperature, 0.95);
    EXPECT_EQ(client.config().numCtx, 4096);
}

TEST(OllamaClientLiveTest, IsModelAvailableFalseForEmptyModelString) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaConfig config = cppcoder_test::LiveOllamaConfig();
    config.model.clear();
    OllamaClient client(config);

    EXPECT_TRUE(client.IsServerReachable());
    EXPECT_FALSE(client.IsModelAvailable());
}

TEST(OllamaClientLiveTest, GenerateCalledTwiceSucceedsIndependently) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());

    std::optional<std::string> first = client.Generate("What is 2+2? Answer with just the number.");
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(first->empty());

    std::optional<std::string> second = client.Generate("What is 3+3? Answer with just the number.");
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE(second->empty());
}

TEST(OllamaClientLiveTest, GeneratePromptWithSpecialCharactersReturnsContent) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    std::optional<std::string> result =
        client.Generate(R"(Echo this token exactly: "quotes", 100% & <tags> {braces} [brackets] \backslash\.)");

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());
}

TEST(OllamaClientLiveTest, ListModelsEntriesAllNonEmpty) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    std::vector<std::string> models = client.ListModels();

    ASSERT_FALSE(models.empty());
    for (const std::string& name : models) {
        EXPECT_FALSE(name.empty());
    }
}

TEST(OllamaClientLiveTest, IsServerReachableIndependentOfConfiguredModel) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaConfig config = cppcoder_test::LiveOllamaConfig();
    config.model = "definitely-not-a-real-model:999b";
    OllamaClient client(config);

    // IsServerReachable only checks /api/tags reachability, not whether
    // the configured model exists.
    EXPECT_TRUE(client.IsServerReachable());
}
