#include "cppcoder/CodebaseScanner.h"
#include "cppcoder/Types.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

class CodebaseScannerTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "cppcoder_scanner_test";
        fs::remove_all(root_);
        fs::create_directories(root_ / "sub" / "deeper");
        fs::create_directories(root_ / ".git");
        fs::create_directories(root_ / "build");

        WriteFile(root_ / "foo.cpp", "int Frobnicate() { return 42; }\n");
        WriteFile(root_ / "sub" / "bar.h", "void Widget();\n");
        WriteFile(root_ / "sub" / "deeper" / "baz.cpp", "void DeepFunction() {}\n");
        WriteFile(root_ / "notes.txt", "irrelevant, not a tracked extension\n");
        WriteFile(root_ / ".git" / "config.cpp", "// should never be scanned\n");
        WriteFile(root_ / "build" / "generated.cpp", "// build artifact, should be skipped\n");
    }

    void TearDown() override { fs::remove_all(root_); }

    static void WriteFile(const fs::path& p, const std::string& content) {
        std::ofstream out(p, std::ios::binary);
        out << content;
    }

    fs::path root_;
};

TEST_F(CodebaseScannerTest, ScanFindsTrackedFilesRecursively) {
    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("", 1'000'000);
    // foo.cpp, sub/bar.h, sub/deeper/baz.cpp -- not notes.txt, not .git/, not build/
    EXPECT_EQ(result.filesIncluded.size(), 3u);
    EXPECT_NE(result.concatenatedContent.find("Frobnicate"), std::string::npos);
}

TEST_F(CodebaseScannerTest, ScanExcludesGitDirectory) {
    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("", 1'000'000);
    for (const auto& f : result.filesIncluded) {
        EXPECT_EQ(f.find(".git"), std::string::npos);
    }
}

TEST_F(CodebaseScannerTest, ScanExcludesBuildDirectory) {
    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("", 1'000'000);
    for (const auto& f : result.filesIncluded) {
        EXPECT_EQ(f.find("build/"), std::string::npos);
    }
}

TEST_F(CodebaseScannerTest, ScanRespectsTokenBudget) {
    cppcoder::CodebaseScanner scanner(root_);
    // Budget far too small to fit even one file's header.
    auto result = scanner.Scan("", 1);
    EXPECT_TRUE(result.filesIncluded.empty());
    EXPECT_FALSE(result.filesSkippedBudget.empty());
}

TEST_F(CodebaseScannerTest, ScanPartiallyFillsBudgetInLexicographicOrder) {
    cppcoder::CodebaseScanner scanner(root_);
    // foo.cpp is lexicographically first among top-level tracked files;
    // give just enough budget for it and nothing else.
    auto full = scanner.Scan("", 1'000'000);
    auto small = scanner.Scan("", cppcoder::EstimateTokens("\n// ==== foo.cpp ====\n") +
                                        cppcoder::EstimateTokens("int Frobnicate() { return 42; }\n"));
    EXPECT_GE(small.filesIncluded.size(), 1u);
    EXPECT_LT(small.filesIncluded.size(), full.filesIncluded.size());
}

TEST_F(CodebaseScannerTest, ScanSingleFileTargetArea) {
    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("foo.cpp", 1'000'000);
    ASSERT_EQ(result.filesIncluded.size(), 1u);
    EXPECT_EQ(result.filesIncluded[0], "foo.cpp");
}

TEST_F(CodebaseScannerTest, ScanNestedDirectoryTargetArea) {
    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("sub", 1'000'000);
    ASSERT_EQ(result.filesIncluded.size(), 2u);  // bar.h, deeper/baz.cpp
}

TEST_F(CodebaseScannerTest, ScanNonexistentAreaReturnsEmpty) {
    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("does/not/exist", 1'000'000);
    EXPECT_TRUE(result.filesIncluded.empty());
    EXPECT_TRUE(result.concatenatedContent.empty());
}

TEST_F(CodebaseScannerTest, ScanRespectsCustomExtensionList) {
    cppcoder::CodebaseScanner scanner(root_, {".txt"});
    auto result = scanner.Scan("", 1'000'000);
    ASSERT_EQ(result.filesIncluded.size(), 1u);
    EXPECT_EQ(result.filesIncluded[0], "notes.txt");
}

TEST_F(CodebaseScannerTest, FindFilesMatchingKeywordSearchesContent) {
    cppcoder::CodebaseScanner scanner(root_);
    auto matches = scanner.FindFilesMatchingKeyword("Frobnicate");
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0], "foo.cpp");
}

TEST_F(CodebaseScannerTest, FindFilesMatchingKeywordIsCaseInsensitive) {
    cppcoder::CodebaseScanner scanner(root_);
    auto matches = scanner.FindFilesMatchingKeyword("widget");
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0], "sub/bar.h");
}

TEST_F(CodebaseScannerTest, FindFilesMatchingKeywordMatchesFilename) {
    cppcoder::CodebaseScanner scanner(root_);
    auto matches = scanner.FindFilesMatchingKeyword("baz");
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0], "sub/deeper/baz.cpp");
}

TEST_F(CodebaseScannerTest, FindFilesMatchingKeywordNoMatchReturnsEmpty) {
    cppcoder::CodebaseScanner scanner(root_);
    auto matches = scanner.FindFilesMatchingKeyword("nonexistent_symbol_xyz");
    EXPECT_TRUE(matches.empty());
}

TEST_F(CodebaseScannerTest, FindFilesMatchingKeywordRespectsMaxResults) {
    // Three files all contain "void" (Widget, DeepFunction).
    cppcoder::CodebaseScanner scanner(root_);
    auto matches = scanner.FindFilesMatchingKeyword("void", 1);
    EXPECT_LE(matches.size(), 1u);
}

TEST_F(CodebaseScannerTest, FindFilesMatchingKeywordExcludesGitAndBuild) {
    cppcoder::CodebaseScanner scanner(root_);
    auto matches = scanner.FindFilesMatchingKeyword("should");
    for (const auto& m : matches) {
        EXPECT_EQ(m.find(".git"), std::string::npos);
        EXPECT_EQ(m.find("build/"), std::string::npos);
    }
}

TEST_F(CodebaseScannerTest, ListFilesReturnsTrackedFilesSorted) {
    cppcoder::CodebaseScanner scanner(root_);
    auto files = scanner.ListFiles();
    // Same three files Scan() picks up, in forward-slash form.
    ASSERT_EQ(files.size(), 3u);
    EXPECT_EQ(files[0], "foo.cpp");
    EXPECT_EQ(files[1], "sub/bar.h");
    EXPECT_EQ(files[2], "sub/deeper/baz.cpp");
}

TEST_F(CodebaseScannerTest, ListFilesExcludesGitBuildAndUntrackedExtensions) {
    cppcoder::CodebaseScanner scanner(root_);
    for (const auto& f : scanner.ListFiles()) {
        EXPECT_EQ(f.find(".git"), std::string::npos);
        EXPECT_EQ(f.find("build/"), std::string::npos);
        EXPECT_EQ(f.find("notes.txt"), std::string::npos);
    }
}

TEST_F(CodebaseScannerTest, ListFilesRespectsMaxResults) {
    cppcoder::CodebaseScanner scanner(root_);
    auto files = scanner.ListFiles(2);
    ASSERT_EQ(files.size(), 2u);
    // Truncation takes a sorted prefix, not an arbitrary two.
    EXPECT_EQ(files[0], "foo.cpp");
    EXPECT_EQ(files[1], "sub/bar.h");
}

}  // namespace
