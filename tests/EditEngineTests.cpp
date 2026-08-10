#include "cppcoder/EditEngine.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

class EditEngineSeedTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "cppcoder_edit_engine_test";
        fs::remove_all(root_);
        fs::create_directories(root_);
        std::ofstream(root_ / "crypto.cpp") << "int DeriveKey() { return 0; }\n";
        std::ofstream(root_ / "parser.cpp") << "void ParseDocument() {}\n";
    }
    void TearDown() override { fs::remove_all(root_); }
    fs::path root_;
};

}  // namespace

TEST_F(EditEngineSeedTest, ReturnsRepeatableSeedTaskWhenKeywordsMatch) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::EditEngine engine(client, scanner, root_, cppcoder::EditEngineConfig{});

    auto tasks = engine.SeedInitialTasks("Rename DeriveKey to ComputeKey", {"DeriveKey"});
    ASSERT_EQ(tasks.size(), 1u);
    EXPECT_TRUE(tasks[0].repeatable);
    ASSERT_EQ(tasks[0].repeatTargets.size(), 1u);
    EXPECT_EQ(tasks[0].repeatTargets[0], "crypto.cpp");
    EXPECT_EQ(tasks[0].researchGoal, "Rename DeriveKey to ComputeKey");
}

TEST_F(EditEngineSeedTest, ReturnsEmptyWhenNoKeywordsMatch) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::EditEngine engine(client, scanner, root_, cppcoder::EditEngineConfig{});

    auto tasks = engine.SeedInitialTasks("task", {"NonexistentSymbolXYZ"});
    EXPECT_TRUE(tasks.empty());
}

TEST_F(EditEngineSeedTest, DedupesFilesMatchedByMultipleKeywords) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::EditEngine engine(client, scanner, root_, cppcoder::EditEngineConfig{});

    // Both keywords match crypto.cpp; it should only appear once.
    auto tasks = engine.SeedInitialTasks("t", {"DeriveKey", "int"});
    ASSERT_EQ(tasks.size(), 1u);
    int count = 0;
    for (const auto& t : tasks[0].repeatTargets) {
        if (t == "crypto.cpp") count++;
    }
    EXPECT_EQ(count, 1);
}

TEST_F(EditEngineSeedTest, MergesMatchesAcrossMultipleKeywords) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::EditEngine engine(client, scanner, root_, cppcoder::EditEngineConfig{});

    auto tasks = engine.SeedInitialTasks("t", {"DeriveKey", "ParseDocument"});
    ASSERT_EQ(tasks.size(), 1u);
    EXPECT_EQ(tasks[0].repeatTargets.size(), 2u);
}

TEST_F(EditEngineSeedTest, SeedTaskDefaultsToDryRun) {
    cppcoder::EditEngineConfig config;
    EXPECT_FALSE(config.apply);
}

TEST(EditEngineConfigTest, DefaultValuesMatchDocumentedDefaults) {
    cppcoder::EditEngineConfig config;
    EXPECT_EQ(config.tokenBudgetPerTask, 120000u);
    EXPECT_EQ(config.maxIterations, 200);
    EXPECT_EQ(config.maxWallClock, std::chrono::minutes(90));
    EXPECT_EQ(config.maxInitialKeywords, 5u);
    EXPECT_FALSE(config.apply);
}

TEST(EditEngineConfigTest, ApplyTrueRoundTrips) {
    cppcoder::EditEngineConfig config;
    config.apply = true;
    EXPECT_TRUE(config.apply);

    cppcoder::EditEngineConfig explicitConfig{120000, 200, std::chrono::minutes(90), 5, true};
    EXPECT_TRUE(explicitConfig.apply);
}

TEST(EditRunResultTest, DefaultValuesAreEmpty) {
    cppcoder::EditRunResult result;
    EXPECT_FALSE(result.anyEdits);
    EXPECT_TRUE(result.proposedEdits.empty());
    EXPECT_TRUE(result.applyOutcome.writtenPaths.empty());
    EXPECT_TRUE(result.applyOutcome.rejectedPaths.empty());
    EXPECT_TRUE(result.applyOutcome.errors.empty());
    EXPECT_EQ(result.iterationsRun, 0);
    EXPECT_EQ(result.wallClock, std::chrono::milliseconds(0));
    EXPECT_TRUE(result.terminationReason.empty());
}

TEST_F(EditEngineSeedTest, SeedInitialTasksWithEmptyKeywordsReturnsNoTasks) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::EditEngine engine(client, scanner, root_, cppcoder::EditEngineConfig{});

    auto tasks = engine.SeedInitialTasks("Do something", {});
    EXPECT_TRUE(tasks.empty());
}

TEST_F(EditEngineSeedTest, SeedInitialTasksWithNoMatchingKeywordsReturnsEmpty) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::EditEngine engine(client, scanner, root_, cppcoder::EditEngineConfig{});

    auto tasks = engine.SeedInitialTasks("task", {"TotallyAbsentSymbol", "AnotherAbsentOne"});
    EXPECT_TRUE(tasks.empty());
}

TEST_F(EditEngineSeedTest, SeedInitialTasksWithKeywordMatchingSingleFile) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::EditEngine engine(client, scanner, root_, cppcoder::EditEngineConfig{});

    // "ParseDocument" only appears in parser.cpp.
    auto tasks = engine.SeedInitialTasks("Refactor the parser", {"ParseDocument"});
    ASSERT_EQ(tasks.size(), 1u);
    ASSERT_EQ(tasks[0].repeatTargets.size(), 1u);
    EXPECT_EQ(tasks[0].repeatTargets[0], "parser.cpp");
}

TEST_F(EditEngineSeedTest, SeedInitialTasksSeedTaskHasZeroDepthAndEditSeedId) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::EditEngine engine(client, scanner, root_, cppcoder::EditEngineConfig{});

    auto tasks = engine.SeedInitialTasks("Rename DeriveKey", {"DeriveKey"});
    ASSERT_EQ(tasks.size(), 1u);
    EXPECT_EQ(tasks[0].id, "edit-seed");
    EXPECT_EQ(tasks[0].depth, 0);
    EXPECT_TRUE(tasks[0].parentId.empty());
    EXPECT_TRUE(tasks[0].repeatable);
    EXPECT_EQ(tasks[0].successCriteria,
              "Made the change described in the task, fully and correctly, across every file "
              "that needed it.");
}

TEST_F(EditEngineSeedTest, SeedInitialTasksPreservesPunctuationInTaskDescription) {
    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::EditEngine engine(client, scanner, root_, cppcoder::EditEngineConfig{});

    const std::string description =
        "Fix DeriveKey(): it shouldn't return 0 -- check edge-cases! (see ticket #42)";
    auto tasks = engine.SeedInitialTasks(description, {"DeriveKey"});
    ASSERT_EQ(tasks.size(), 1u);
    EXPECT_EQ(tasks[0].researchGoal, description);
}

TEST_F(EditEngineSeedTest, SeedInitialTasksCoversFilesAcrossMultipleSubdirectories) {
    fs::create_directories(root_ / "sub1");
    fs::create_directories(root_ / "sub2" / "nested");
    std::ofstream(root_ / "sub1" / "widget.cpp") << "void MultiDirProbe() {}\n";
    std::ofstream(root_ / "sub2" / "nested" / "gadget.cpp") << "void MultiDirProbe() {}\n";

    cppcoder::OllamaClient client(cppcoder::OllamaConfig{});
    cppcoder::CodebaseScanner scanner(root_);
    cppcoder::EditEngine engine(client, scanner, root_, cppcoder::EditEngineConfig{});

    auto tasks = engine.SeedInitialTasks("Update MultiDirProbe everywhere", {"MultiDirProbe"});
    ASSERT_EQ(tasks.size(), 1u);
    ASSERT_EQ(tasks[0].repeatTargets.size(), 2u);

    bool foundSub1 = false;
    bool foundSub2Nested = false;
    for (const auto& target : tasks[0].repeatTargets) {
        if (target.find("sub1") != std::string::npos) foundSub1 = true;
        if (target.find("sub2") != std::string::npos && target.find("nested") != std::string::npos)
            foundSub2Nested = true;
    }
    EXPECT_TRUE(foundSub1);
    EXPECT_TRUE(foundSub2Nested);
}
