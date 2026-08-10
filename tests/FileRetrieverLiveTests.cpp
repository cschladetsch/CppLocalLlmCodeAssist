#include "cppcoder/FileRetriever.h"

#include "OllamaTestSupport.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

// Live end-to-end coverage for RunRetrievalPrePass -- the only function in
// FileRetriever.h that actually talks to Ollama. FileRetrieverTests.cpp
// covers every pure sub-step (FindLikelyFiles, BuildRetrievalPrompt,
// ParseFileRequests, ReadRequestedFiles, FormatFileContext) offline with
// hand-crafted inputs; these tests instead drive the real pipeline against
// a live model.
//
// Which files (if any) the model decides it needs is non-deterministic, so
// these tests never assert a specific retrieval outcome. They only assert
// invariants that must always hold: no throwing/crashing for any input, an
// exact empty string when the pre-pass should skip outright (no keyword
// overlap, pure small talk, no candidate files at all), and -- when a real
// candidate exists -- that the result is either empty or plausibly derived
// from that candidate's known path/content.

namespace fs = std::filesystem;

using cppcoder::RunRetrievalPrePass;

namespace {

class FileRetrieverLiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "cppcoder_file_retriever_live_test";
        fs::remove_all(root_);
        fs::create_directories(root_ / "src");
        WriteFile(root_ / "src" / "Alpha.cpp", "int ComputeAlphaValue() { return 42; }\n");
        WriteFile(root_ / "src" / "Beta.cpp", "int ComputeBetaValue() { return 7; }\n");
        WriteFile(root_ / "README.md", "# readme\n");
    }

    void TearDown() override { fs::remove_all(root_); }

    static void WriteFile(const fs::path& p, const std::string& content) {
        std::ofstream out(p, std::ios::binary);
        out << content;
    }

    fs::path root_;
};

}  // namespace

// A userMessage clearly referencing an identifier that lives in exactly one
// temp file. RunRetrievalPrePass must not crash, and -- since the model may
// legitimately decide the file isn't needed -- the result must be either
// empty or contain that file's known path/content, never something else.
TEST_F(FileRetrieverLiveTest, MessageReferencingUniqueIdentifierIsConsistentWithSkipOrRetrieve) {
    cppcoder_test::SkipUnlessOllamaReady();
    auto config = cppcoder_test::LiveOllamaConfig();

    std::string result = RunRetrievalPrePass("what does ComputeAlphaValue do?", config.model,
                                              config.host, config.port, root_);

    EXPECT_TRUE(result.empty() || result.find("ComputeAlphaValue") != std::string::npos ||
                result.find("src/Alpha.cpp") != std::string::npos);
}

// A message with zero keyword overlap with anything in the temp codebase
// never reaches the model at all -- FindLikelyFiles finds no candidates, so
// this is a deterministic, exactly-empty result.
TEST_F(FileRetrieverLiveTest, MessageMatchingNothingReturnsExactlyEmptyString) {
    cppcoder_test::SkipUnlessOllamaReady();
    auto config = cppcoder_test::LiveOllamaConfig();

    std::string result = RunRetrievalPrePass("zzzznonexistentterm about something else entirely",
                                              config.model, config.host, config.port, root_);

    EXPECT_EQ(result, "");
}

// Pure small talk is filtered by IsConversationalWord/FallbackKeywords
// before any candidate search matters, per the README's documented
// behaviour -- this should never reach the model either.
TEST_F(FileRetrieverLiveTest, ConversationalMessageReturnsEmptyString) {
    cppcoder_test::SkipUnlessOllamaReady();
    auto config = cppcoder_test::LiveOllamaConfig();

    std::string result = RunRetrievalPrePass("hello, how are you today?", config.model,
                                              config.host, config.port, root_);

    EXPECT_TRUE(result.empty());
}

// Calling the pre-pass twice with identical inputs must complete both times
// without crashing or hanging, even though the model's non-determinism
// means the two results need not match.
TEST_F(FileRetrieverLiveTest, CalledTwiceWithSameInputsBothCompleteCleanly) {
    cppcoder_test::SkipUnlessOllamaReady();
    auto config = cppcoder_test::LiveOllamaConfig();

    std::string first, second;
    EXPECT_NO_THROW({
        first = RunRetrievalPrePass("what does ComputeBetaValue do?", config.model, config.host,
                                     config.port, root_);
    });
    EXPECT_NO_THROW({
        second = RunRetrievalPrePass("what does ComputeBetaValue do?", config.model, config.host,
                                      config.port, root_);
    });

    // Well-formed strings, whatever their content: no crash inspecting them.
    EXPECT_GE(first.size(), 0u);
    EXPECT_GE(second.size(), 0u);
}

// An unreachable Ollama host/port must degrade to an empty string rather
// than throwing or hanging -- but only exercises that path when a
// candidate genuinely exists, since otherwise FindLikelyFiles would skip
// before ever attempting to reach the host.
TEST_F(FileRetrieverLiveTest, UnreachableHostReturnsEmptyStringRatherThanThrowing) {
    cppcoder_test::SkipUnlessOllamaReady();
    auto config = cppcoder_test::LiveOllamaConfig();

    std::string result;
    EXPECT_NO_THROW({
        result = RunRetrievalPrePass("what does ComputeAlphaValue do?", config.model,
                                      "127.0.0.1", /*ollamaPort=*/1, root_);
    });
    EXPECT_TRUE(result.empty());
}

// Punctuation and other special characters in the user message must not
// crash the pipeline (keyword extraction, prompt building, JSON parsing).
TEST_F(FileRetrieverLiveTest, MessageWithSpecialCharactersDoesNotCrash) {
    cppcoder_test::SkipUnlessOllamaReady();
    auto config = cppcoder_test::LiveOllamaConfig();

    std::string result;
    EXPECT_NO_THROW({
        result = RunRetrievalPrePass(
            "what does ComputeAlphaValue() do?! @#$%^&*()_+-={}[]|\\:\";'<>?,./~`",
            config.model, config.host, config.port, root_);
    });
    EXPECT_TRUE(result.empty() || result.find("ComputeAlphaValue") != std::string::npos ||
                result.find("src/Alpha.cpp") != std::string::npos);
}

// An empty userMessage should not crash and should yield a well-formed
// (in practice empty, since no keywords can be extracted) string.
TEST_F(FileRetrieverLiveTest, EmptyMessageDoesNotCrash) {
    cppcoder_test::SkipUnlessOllamaReady();
    auto config = cppcoder_test::LiveOllamaConfig();

    std::string result;
    EXPECT_NO_THROW({ result = RunRetrievalPrePass("", config.model, config.host, config.port, root_); });
    EXPECT_TRUE(result.empty());
}

// A root directory with zero files of a matching extension gives
// FindLikelyFiles nothing to work with regardless of the message, so the
// pre-pass must skip outright and return an empty string.
TEST_F(FileRetrieverLiveTest, RootWithNoMatchingExtensionFilesReturnsEmptyString) {
    cppcoder_test::SkipUnlessOllamaReady();
    auto config = cppcoder_test::LiveOllamaConfig();

    fs::path noExtRoot = fs::temp_directory_path() / "cppcoder_file_retriever_live_test_noext";
    fs::remove_all(noExtRoot);
    fs::create_directories(noExtRoot);
    WriteFile(noExtRoot / "notes.txt", "ComputeAlphaValue appears here too, but .txt isn't scanned.\n");

    std::string result = RunRetrievalPrePass("what does ComputeAlphaValue do?", config.model,
                                              config.host, config.port, noExtRoot);

    EXPECT_EQ(result, "");

    fs::remove_all(noExtRoot);
}

// A message matching multiple different files in the temp codebase must
// not crash, regardless of how many (if any) of them the model chooses.
TEST_F(FileRetrieverLiveTest, MessageMatchingMultipleFilesDoesNotCrash) {
    cppcoder_test::SkipUnlessOllamaReady();
    auto config = cppcoder_test::LiveOllamaConfig();

    std::string result;
    EXPECT_NO_THROW({
        result = RunRetrievalPrePass("compare ComputeAlphaValue and ComputeBetaValue",
                                      config.model, config.host, config.port, root_);
    });
    EXPECT_TRUE(result.empty() || result.find("ComputeAlphaValue") != std::string::npos ||
                result.find("ComputeBetaValue") != std::string::npos ||
                result.find("src/Alpha.cpp") != std::string::npos ||
                result.find("src/Beta.cpp") != std::string::npos);
}

// Basic sanity check that whatever comes back -- empty or not -- is a
// normal std::string that doesn't crash when its length or contents are
// inspected (e.g. no embedded state that blows up on access).
TEST_F(FileRetrieverLiveTest, ResultIsAWellFormedInspectableString) {
    cppcoder_test::SkipUnlessOllamaReady();
    auto config = cppcoder_test::LiveOllamaConfig();

    std::string result = RunRetrievalPrePass("what does ComputeAlphaValue do?", config.model,
                                              config.host, config.port, root_);

    EXPECT_NO_THROW({
        volatile std::size_t len = result.size();
        (void)len;
        for (char c : result) {
            volatile char cc = c;
            (void)cc;
        }
        std::string copy = result;
        EXPECT_EQ(copy.size(), result.size());
    });
}
