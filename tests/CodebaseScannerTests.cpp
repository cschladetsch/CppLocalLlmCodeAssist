#include "cppcoder/CodebaseScanner.h"
#include "cppcoder/Types.h"

#include <gtest/gtest.h>

#include <algorithm>
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

TEST_F(CodebaseScannerTest, ScanTargetAreaPointingDirectlyAtNestedFile) {
    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("sub/deeper/baz.cpp", 1'000'000);
    ASSERT_EQ(result.filesIncluded.size(), 1u);
    EXPECT_EQ(result.filesIncluded[0], "sub/deeper/baz.cpp");
    EXPECT_NE(result.concatenatedContent.find("DeepFunction"), std::string::npos);
}

TEST_F(CodebaseScannerTest, ScanWithEmptyExtensionListMatchesNoFiles) {
    cppcoder::CodebaseScanner scanner(root_, {});
    auto result = scanner.Scan("", 1'000'000);
    EXPECT_TRUE(result.filesIncluded.empty());
    EXPECT_TRUE(result.concatenatedContent.empty());
    EXPECT_EQ(result.approxTokens, 0u);
}

TEST_F(CodebaseScannerTest, ScanExcludesExcludedDirectoryNestedSeveralLevelsDeep) {
    fs::create_directories(root_ / "sub" / "deeper" / "build");
    WriteFile(root_ / "sub" / "deeper" / "build" / "artifact.cpp",
               "// nested build artifact, should be skipped\n");

    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("", 1'000'000);
    ASSERT_EQ(result.filesIncluded.size(), 3u);  // unchanged from the base fixture
    for (const auto& f : result.filesIncluded) {
        EXPECT_EQ(f.find("artifact"), std::string::npos);
    }
}

TEST_F(CodebaseScannerTest, ScanDoesNotExcludeDirectoryNameThatIsSubstringOfExcludedName) {
    fs::create_directories(root_ / "builder");
    WriteFile(root_ / "builder" / "code.cpp", "int BuilderCode() { return 7; }\n");

    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("", 1'000'000);
    // "builder" is not the exact path component "build", so it must survive.
    ASSERT_EQ(result.filesIncluded.size(), 4u);
    EXPECT_NE(std::find(result.filesIncluded.begin(), result.filesIncluded.end(),
                         "builder/code.cpp"),
              result.filesIncluded.end());
}

TEST_F(CodebaseScannerTest, ScanIgnoresFilesWithUntrackedExtensionEntirely) {
    WriteFile(root_ / "readme.md", "# not a tracked extension, ignore entirely\n");

    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("", 1'000'000);
    ASSERT_EQ(result.filesIncluded.size(), 3u);
    for (const auto& f : result.filesIncluded) {
        EXPECT_EQ(f.find("readme"), std::string::npos);
    }
    for (const auto& f : result.filesSkippedBudget) {
        EXPECT_EQ(f.find("readme"), std::string::npos);
    }
}

TEST_F(CodebaseScannerTest, ScanEmptyDirectoryReturnsEmptyResultWithZeroTokens) {
    fs::create_directories(root_ / "emptydir");

    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("emptydir", 1'000'000);
    EXPECT_TRUE(result.filesIncluded.empty());
    EXPECT_TRUE(result.filesSkippedBudget.empty());
    EXPECT_TRUE(result.concatenatedContent.empty());
    EXPECT_EQ(result.approxTokens, 0u);
}

TEST_F(CodebaseScannerTest, ScanZeroTokenBudgetSkipsAllFiles) {
    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("", 0);
    EXPECT_TRUE(result.filesIncluded.empty());
    EXPECT_EQ(result.approxTokens, 0u);
    EXPECT_EQ(result.filesSkippedBudget.size(), 3u);
}

TEST_F(CodebaseScannerTest, ScanBudgetSmallerThanSmallestFileSkipsAll) {
    cppcoder::CodebaseScanner scanner(root_);
    // sub/bar.h is the smallest tracked file in the fixture; one token less
    // than it needs must be too small for every file, not just that one.
    const std::string smallestContent = "void Widget();\n";
    const std::string smallestHeader = "\n// ==== sub/bar.h ====\n";
    const std::size_t needed =
        cppcoder::EstimateTokens(smallestHeader) + cppcoder::EstimateTokens(smallestContent);
    ASSERT_GT(needed, 0u);

    auto result = scanner.Scan("", needed - 1);
    EXPECT_TRUE(result.filesIncluded.empty());
    EXPECT_EQ(result.filesSkippedBudget.size(), 3u);
}

TEST_F(CodebaseScannerTest, ScanBudgetFitsSomeFilesNotAllReportsSkippedNames) {
    fs::create_directories(root_ / "budgetarea");
    const std::string smallContent = "int A() { return 1; }\n";
    const std::string largeContent(500, 'x');
    WriteFile(root_ / "budgetarea" / "a.cpp", smallContent);
    WriteFile(root_ / "budgetarea" / "b.cpp", largeContent);

    const std::string smallHeader = "\n// ==== budgetarea/a.cpp ====\n";
    const std::size_t budget =
        cppcoder::EstimateTokens(smallHeader) + cppcoder::EstimateTokens(smallContent);

    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("budgetarea", budget);
    ASSERT_EQ(result.filesIncluded.size(), 1u);
    EXPECT_EQ(result.filesIncluded[0], "budgetarea/a.cpp");
    ASSERT_EQ(result.filesSkippedBudget.size(), 1u);
    EXPECT_EQ(result.filesSkippedBudget[0], "budgetarea/b.cpp");
}

TEST_F(CodebaseScannerTest, FindFilesMatchingKeywordAbsentEverywhereReturnsEmpty) {
    cppcoder::CodebaseScanner scanner(root_);
    auto matches = scanner.FindFilesMatchingKeyword("qzxjklw_absolutely_missing");
    EXPECT_TRUE(matches.empty());
}

TEST_F(CodebaseScannerTest, FindFilesMatchingKeywordMaxResultsOneTruncatesFilenameMatches) {
    // "cpp" is a substring of both foo.cpp's and sub/deeper/baz.cpp's
    // filenames; the result order isn't sorted (unlike ListFiles/Scan), so
    // only assert the truncation count and that it's one of the two.
    cppcoder::CodebaseScanner scanner(root_);
    auto matches = scanner.FindFilesMatchingKeyword("cpp", 1);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_TRUE(matches[0] == "foo.cpp" || matches[0] == "sub/deeper/baz.cpp");
}

TEST_F(CodebaseScannerTest, FindFilesMatchingKeywordSubstringInsideLongerIdentifier) {
    cppcoder::CodebaseScanner scanner(root_);
    // "robnica" is only a fragment of "Frobnicate", not a whole word.
    auto matches = scanner.FindFilesMatchingKeyword("robnica");
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0], "foo.cpp");
}

TEST_F(CodebaseScannerTest, ListFilesMaxResultsSmallerThanTotalTruncatesWithoutCrash) {
    cppcoder::CodebaseScanner scanner(root_);
    auto files = scanner.ListFiles(1);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0], "foo.cpp");
}

TEST_F(CodebaseScannerTest, ListFilesUsesForwardSlashesOnAllPlatforms) {
    cppcoder::CodebaseScanner scanner(root_);
    auto files = scanner.ListFiles();
    ASSERT_FALSE(files.empty());
    bool sawNestedPath = false;
    for (const auto& f : files) {
        EXPECT_EQ(f.find('\\'), std::string::npos);
        if (f.find('/') != std::string::npos) sawNestedPath = true;
    }
    EXPECT_TRUE(sawNestedPath);
}

TEST_F(CodebaseScannerTest, ListFilesResultsAreSortedAlphabetically) {
    fs::create_directories(root_ / "unsorted");
    // Create the "later" file first so any non-sorting iterator order would
    // surface it before the "earlier" one if ListFiles didn't sort.
    WriteFile(root_ / "unsorted" / "zzz.cpp", "void Last() {}\n");
    WriteFile(root_ / "unsorted" / "aaa.cpp", "void First() {}\n");

    cppcoder::CodebaseScanner scanner(root_);
    auto files = scanner.ListFiles();
    EXPECT_TRUE(std::is_sorted(files.begin(), files.end()));

    auto aaaIt = std::find(files.begin(), files.end(), "unsorted/aaa.cpp");
    auto zzzIt = std::find(files.begin(), files.end(), "unsorted/zzz.cpp");
    ASSERT_NE(aaaIt, files.end());
    ASSERT_NE(zzzIt, files.end());
    EXPECT_LT(aaaIt - files.begin(), zzzIt - files.begin());
}

TEST_F(CodebaseScannerTest, ScanExtensionMatchingIsCaseSensitiveForUppercaseVariant) {
    WriteFile(root_ / "Legacy.CPP", "// uppercase extension, should not match \".cpp\"\n");

    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("", 1'000'000);
    ASSERT_EQ(result.filesIncluded.size(), 3u);  // unchanged from the base fixture
    for (const auto& f : result.filesIncluded) {
        EXPECT_EQ(f.find("Legacy"), std::string::npos);
    }
}

TEST_F(CodebaseScannerTest, ScanRespectsMultipleExcludedDirsSimultaneously) {
    fs::create_directories(root_ / "vendor");
    fs::create_directories(root_ / "tmp");
    WriteFile(root_ / "vendor" / "lib.cpp", "// vendored dependency\n");
    WriteFile(root_ / "tmp" / "scratch.cpp", "// scratch file\n");

    cppcoder::CodebaseScanner scanner(
        root_, {".cpp", ".h", ".hpp", ".cc", ".cxx", ".py", ".rs", ".scala"},
        {".git", "build", "vendor", "tmp"});
    auto result = scanner.Scan("", 1'000'000);
    ASSERT_EQ(result.filesIncluded.size(), 3u);  // unchanged from the base fixture
    for (const auto& f : result.filesIncluded) {
        EXPECT_EQ(f.find("vendor"), std::string::npos);
        EXPECT_EQ(f.find("tmp"), std::string::npos);
    }
}

TEST_F(CodebaseScannerTest, ScanRecursesThroughDeeplyNestedDirectoryStructure) {
    fs::create_directories(root_ / "a" / "b" / "c" / "d" / "e");
    WriteFile(root_ / "a" / "b" / "c" / "d" / "e" / "leaf.cpp", "void Leaf() {}\n");

    cppcoder::CodebaseScanner scanner(root_);
    auto result = scanner.Scan("", 1'000'000);
    ASSERT_EQ(result.filesIncluded.size(), 4u);
    EXPECT_NE(std::find(result.filesIncluded.begin(), result.filesIncluded.end(),
                         "a/b/c/d/e/leaf.cpp"),
              result.filesIncluded.end());
    EXPECT_NE(result.concatenatedContent.find("Leaf"), std::string::npos);
}

}  // namespace
