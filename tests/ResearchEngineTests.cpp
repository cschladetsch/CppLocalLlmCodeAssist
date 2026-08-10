#include "cppcoder/ResearchEngine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ---------------- FallbackKeywords ----------------

TEST(FallbackKeywordsTest, ExtractsMeaningfulWords) {
    auto kws = cppcoder::FallbackKeywords("How is the encryption key derived for PDF documents?", 5);
    EXPECT_FALSE(kws.empty());
    // "how", "is", "the", "for" are stopwords / too short and should be excluded.
    for (const auto& k : kws) {
        std::string lower = k;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        EXPECT_NE(lower, "how");
        EXPECT_NE(lower, "is");
        EXPECT_NE(lower, "the");
        EXPECT_NE(lower, "for");
    }
}

TEST(FallbackKeywordsTest, RespectsMaxCount) {
    auto kws = cppcoder::FallbackKeywords(
        "encryption decryption parsing tokenizing rendering compiling linking", 3);
    EXPECT_LE(kws.size(), 3u);
}

TEST(FallbackKeywordsTest, EmptyQuestionReturnsEmpty) {
    auto kws = cppcoder::FallbackKeywords("", 5);
    EXPECT_TRUE(kws.empty());
}

TEST(FallbackKeywordsTest, OnlyStopwordsReturnsEmpty) {
    auto kws = cppcoder::FallbackKeywords("how is the a an it", 5);
    EXPECT_TRUE(kws.empty());
}

TEST(FallbackKeywordsTest, ShortWordsUnderThreeCharsExcluded) {
    auto kws = cppcoder::FallbackKeywords("go do it up on ok", 10);
    // All of these are <3 chars or stopwords.
    EXPECT_TRUE(kws.empty());
}

TEST(FallbackKeywordsTest, PreservesOriginalCasing) {
    auto kws = cppcoder::FallbackKeywords("PdfCrypto handles Encryption", 5);
    bool foundOriginalCase = false;
    for (const auto& k : kws) {
        if (k == "PdfCrypto") foundOriginalCase = true;
    }
    EXPECT_TRUE(foundOriginalCase);
}

TEST(FallbackKeywordsTest, UnderscoresKeepIdentifierIntact) {
    auto kws = cppcoder::FallbackKeywords("what does parse_document_stream do", 5);
    bool found = false;
    for (const auto& k : kws) {
        if (k == "parse_document_stream") found = true;
    }
    EXPECT_TRUE(found);
}

// ---------------- SeedInitialTasks ----------------

namespace {

class SeedInitialTasksTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "cppcoder_engine_test";
        fs::remove_all(root_);
        fs::create_directories(root_);
        std::ofstream(root_ / "crypto.cpp") << "int DeriveKey() { return 0; }\n";
        std::ofstream(root_ / "parser.cpp") << "void ParseDocument() {}\n";
    }
    void TearDown() override { fs::remove_all(root_); }
    fs::path root_;
};

}  // namespace

TEST_F(SeedInitialTasksTest, ReturnsRepeatableSeedTaskWhenKeywordsMatch) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::ResearchEngine engine(client, scanner, cppcoder::EngineConfig{});

    auto tasks = engine.SeedInitialTasks("How is the key derived?", {"DeriveKey"});
    ASSERT_EQ(tasks.size(), 1u);
    EXPECT_TRUE(tasks[0].repeatable);
    ASSERT_EQ(tasks[0].repeatTargets.size(), 1u);
    EXPECT_EQ(tasks[0].repeatTargets[0], "crypto.cpp");
    EXPECT_EQ(tasks[0].researchGoal, "How is the key derived?");
}

TEST_F(SeedInitialTasksTest, ReturnsEmptyWhenNoKeywordsMatch) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::ResearchEngine engine(client, scanner, cppcoder::EngineConfig{});

    auto tasks = engine.SeedInitialTasks("question", {"NonexistentSymbolXYZ"});
    EXPECT_TRUE(tasks.empty());
}

TEST_F(SeedInitialTasksTest, DedupesFilesMatchedByMultipleKeywords) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::ResearchEngine engine(client, scanner, cppcoder::EngineConfig{});

    // Both keywords match crypto.cpp; it should only appear once.
    auto tasks = engine.SeedInitialTasks("q", {"DeriveKey", "int"});
    ASSERT_EQ(tasks.size(), 1u);
    int count = 0;
    for (const auto& t : tasks[0].repeatTargets) {
        if (t == "crypto.cpp") count++;
    }
    EXPECT_EQ(count, 1);
}

TEST_F(SeedInitialTasksTest, MergesMatchesAcrossMultipleKeywords) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::ResearchEngine engine(client, scanner, cppcoder::EngineConfig{});

    auto tasks = engine.SeedInitialTasks("q", {"DeriveKey", "ParseDocument"});
    ASSERT_EQ(tasks.size(), 1u);
    EXPECT_EQ(tasks[0].repeatTargets.size(), 2u);
}

// ---------------- Additional FallbackKeywords edge cases ----------------

TEST(FallbackKeywordsTest, MaxCountZeroStillReturnsOneMatch) {
    // The size check in FallbackKeywords happens *after* push_back, so a
    // maxCount of 0 does not short-circuit to an empty vector -- the first
    // valid keyword is pushed and then the loop breaks because
    // keywords.size() (1) >= maxCount (0).
    auto kws = cppcoder::FallbackKeywords("encryption decryption parsing", 0);
    ASSERT_EQ(kws.size(), 1u);
    EXPECT_EQ(kws[0], "encryption");
}

TEST(FallbackKeywordsTest, MaxCountOneReturnsExactlyOne) {
    auto kws = cppcoder::FallbackKeywords("encryption decryption parsing tokenizing", 1);
    ASSERT_EQ(kws.size(), 1u);
    EXPECT_EQ(kws[0], "encryption");
}

TEST(FallbackKeywordsTest, DuplicateWordsAreNotDeduped) {
    auto kws = cppcoder::FallbackKeywords("parsing parsing parsing", 5);
    ASSERT_EQ(kws.size(), 3u);
    for (const auto& k : kws) EXPECT_EQ(k, "parsing");
}

TEST(FallbackKeywordsTest, FewerValidWordsThanMaxCountReturnsAllOfThem) {
    auto kws = cppcoder::FallbackKeywords("parsing rendering", 10);
    ASSERT_EQ(kws.size(), 2u);
    EXPECT_EQ(kws[0], "parsing");
    EXPECT_EQ(kws[1], "rendering");
}

TEST(FallbackKeywordsTest, PunctuationSplitsIdentifiersApart) {
    // "::" and "()" are non-alnum/non-underscore, so they act as word
    // separators -- the identifiers on either side come through cleanly
    // with no leftover punctuation attached.
    auto kws = cppcoder::FallbackKeywords("How does Worker::Execute() work?", 5);
    bool foundWorker = false;
    bool foundExecute = false;
    for (const auto& k : kws) {
        EXPECT_EQ(k.find(':'), std::string::npos);
        EXPECT_EQ(k.find('('), std::string::npos);
        EXPECT_EQ(k.find(')'), std::string::npos);
        if (k == "Worker") foundWorker = true;
        if (k == "Execute") foundExecute = true;
    }
    EXPECT_TRUE(foundWorker);
    EXPECT_TRUE(foundExecute);
}

TEST(FallbackKeywordsTest, CamelCaseIdentifierExtractedWhole) {
    auto kws = cppcoder::FallbackKeywords("What is the getUserById function", 5);
    bool found = false;
    for (const auto& k : kws) {
        if (k == "getUserById") found = true;
    }
    EXPECT_TRUE(found);
}

TEST(FallbackKeywordsTest, DigitsWithinIdentifierKeptIntact) {
    auto kws = cppcoder::FallbackKeywords("the sha256_hash value", 5);
    bool found = false;
    for (const auto& k : kws) {
        if (k == "sha256_hash") found = true;
    }
    EXPECT_TRUE(found);
}

// ---------------- Additional SeedInitialTasks edge cases ----------------

TEST_F(SeedInitialTasksTest, EmptyKeywordsListReturnsEmpty) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::ResearchEngine engine(client, scanner, cppcoder::EngineConfig{});

    auto tasks = engine.SeedInitialTasks("question with no keywords supplied", {});
    EXPECT_TRUE(tasks.empty());
}

TEST_F(SeedInitialTasksTest, MixedMatchingAndNonMatchingKeywordsStillReturnsTask) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::ResearchEngine engine(client, scanner, cppcoder::EngineConfig{});

    auto tasks = engine.SeedInitialTasks("q", {"NonexistentSymbolXYZ", "DeriveKey"});
    ASSERT_EQ(tasks.size(), 1u);
    ASSERT_EQ(tasks[0].repeatTargets.size(), 1u);
    EXPECT_EQ(tasks[0].repeatTargets[0], "crypto.cpp");
}

TEST_F(SeedInitialTasksTest, MatchesAcrossMultipleSubdirectories) {
    fs::create_directories(root_ / "sub1");
    fs::create_directories(root_ / "sub2");
    std::ofstream(root_ / "sub1" / "alpha.cpp") << "void AlphaFunc() {}\n";
    std::ofstream(root_ / "sub2" / "beta.cpp") << "void BetaFunc() {}\n";

    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::ResearchEngine engine(client, scanner, cppcoder::EngineConfig{});

    auto tasks = engine.SeedInitialTasks("q", {"AlphaFunc", "BetaFunc"});
    ASSERT_EQ(tasks.size(), 1u);
    ASSERT_EQ(tasks[0].repeatTargets.size(), 2u);
    bool foundSub1 = false;
    bool foundSub2 = false;
    for (const auto& t : tasks[0].repeatTargets) {
        if (t == "sub1/alpha.cpp") foundSub1 = true;
        if (t == "sub2/beta.cpp") foundSub2 = true;
    }
    EXPECT_TRUE(foundSub1);
    EXPECT_TRUE(foundSub2);
}

TEST_F(SeedInitialTasksTest, SeedTaskFieldsPopulatedFromQuestion) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::ResearchEngine engine(client, scanner, cppcoder::EngineConfig{});

    auto tasks = engine.SeedInitialTasks("How is the key derived?", {"DeriveKey"});
    ASSERT_EQ(tasks.size(), 1u);
    const auto& t = tasks[0];
    EXPECT_EQ(t.id, "seed");
    EXPECT_EQ(t.depth, 0);
    EXPECT_TRUE(t.parentId.empty());
    EXPECT_TRUE(t.repeatable);
    EXPECT_EQ(t.researchGoal, "How is the key derived?");
    EXPECT_EQ(t.successCriteria,
              "Found information in the code that directly and fully answers the question above.");
    EXPECT_NE(t.targetArea.find("keyword probe"), std::string::npos);
}

// ---------------- EngineConfig / ResearchResult defaults ----------------

TEST(EngineConfigTest, DefaultValuesMatchSpec) {
    cppcoder::EngineConfig config;
    EXPECT_EQ(config.tokenBudgetPerTask, 120'000u);
    EXPECT_EQ(config.maxIterations, 200);
    EXPECT_EQ(config.maxWallClock, std::chrono::minutes(90));
    EXPECT_EQ(config.maxInitialKeywords, 5u);
}

TEST(ResearchResultTest, DefaultValuesAreUnanswered) {
    cppcoder::ResearchResult result;
    EXPECT_FALSE(result.answered);
    EXPECT_TRUE(result.answer.empty());
    EXPECT_TRUE(result.successfulFindings.empty());
    EXPECT_EQ(result.iterationsRun, 0);
    EXPECT_EQ(result.wallClock, std::chrono::milliseconds(0));
    EXPECT_TRUE(result.terminationReason.empty());
}
