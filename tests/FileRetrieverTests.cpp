#include "cppcoder/FileRetriever.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

using cppcoder::BuildRetrievalPrompt;
using cppcoder::FormatFileContext;
using cppcoder::ParseFileRequests;
using cppcoder::ReadRequestedFiles;
using cppcoder::RetrievedFile;

namespace {

class FileRetrieverTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "cppcoder_file_retriever_test";
        fs::remove_all(root_);
        fs::create_directories(root_ / "src");
        WriteFile(root_ / "src" / "Alpha.cpp", "int alpha() { return 1; }\n");
        WriteFile(root_ / "src" / "Beta.cpp", "int beta() { return 2; }\n");
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

// ---------------- BuildRetrievalPrompt ----------------

TEST(BuildRetrievalPromptTest, IncludesMessageAndCandidateList) {
    std::string prompt =
        BuildRetrievalPrompt("what does alpha do?", {"src/Alpha.cpp", "README.md"});
    EXPECT_NE(prompt.find("what does alpha do?"), std::string::npos);
    EXPECT_NE(prompt.find("src/Alpha.cpp"), std::string::npos);
    EXPECT_NE(prompt.find("README.md"), std::string::npos);
}

TEST(BuildRetrievalPromptTest, AsksForTheEmptyCaseExplicitly) {
    // Without this the planner tends to pick a file for "hello".
    std::string prompt = BuildRetrievalPrompt("hi", {"src/Alpha.cpp"});
    EXPECT_NE(prompt.find("\"files\": []"), std::string::npos);
}

TEST(BuildRetrievalPromptTest, OffersOnlyTheCandidatesGiven) {
    // Regression guard: an earlier version also listed the whole
    // repository, and a 1.5b model would order from that menu for
    // "hello, how are you today?".
    std::string prompt = BuildRetrievalPrompt("alpha", {"src/Alpha.cpp"});
    EXPECT_NE(prompt.find("src/Alpha.cpp"), std::string::npos);
    EXPECT_EQ(prompt.find("src/Beta.cpp"), std::string::npos);
}

// ---------------- FindLikelyFiles ----------------

TEST_F(FileRetrieverTest, FindLikelyFilesMatchesOnContentNotFilename) {
    // The whole point: "alpha" is defined in Alpha.cpp, but a name that
    // gives nothing away still has to be findable by what's inside it.
    WriteFile(root_ / "src" / "Opaque.cpp", "void FindRepoRootHelper() {}\n");
    cppcoder::CodebaseScanner scanner(root_);

    auto likely = FindLikelyFiles("what does FindRepoRootHelper do?", scanner);
    ASSERT_FALSE(likely.empty());
    EXPECT_EQ(likely[0], "src/Opaque.cpp");
}

TEST_F(FileRetrieverTest, FindLikelyFilesRanksMoreMatchesFirst) {
    WriteFile(root_ / "src" / "Both.cpp", "void alpha(); void beta();\n");
    cppcoder::CodebaseScanner scanner(root_);

    auto likely = FindLikelyFiles("compare alpha and beta", scanner);
    ASSERT_FALSE(likely.empty());
    EXPECT_EQ(likely[0], "src/Both.cpp") << "the file matching both terms should rank first";
}

TEST_F(FileRetrieverTest, FindLikelyFilesReturnsNothingForUnrelatedMessage) {
    cppcoder::CodebaseScanner scanner(root_);
    EXPECT_TRUE(FindLikelyFiles("zzzznonexistentterm", scanner).empty());
}

TEST_F(FileRetrieverTest, FindLikelyFilesRespectsMaxResults) {
    cppcoder::CodebaseScanner scanner(root_);
    EXPECT_LE(FindLikelyFiles("int return alpha beta", scanner, 1).size(), 1u);
}

TEST_F(FileRetrieverTest, FindLikelyFilesIgnoresConversationalWords) {
    // "you" survives FallbackKeywords' research-tuned stopword list and
    // appears in a comment in most real files, which is enough to make
    // small talk look like a code question.
    WriteFile(root_ / "src" / "Chatty.cpp", "// returns something for you\nvoid f() {}\n");
    cppcoder::CodebaseScanner scanner(root_);

    EXPECT_TRUE(FindLikelyFiles("hello, how are you today?", scanner).empty());
}

TEST_F(FileRetrieverTest, FindLikelyFilesIgnoresTermsThatMatchEverything) {
    // A term present in every file discriminates nothing. Saturating the
    // per-keyword cap is the signal that it's one of those.
    for (int i = 0; i < 25; ++i) {
        WriteFile(root_ / "src" / ("Common" + std::to_string(i) + ".cpp"), "int ubiquitous;\n");
    }
    cppcoder::CodebaseScanner scanner(root_);

    EXPECT_TRUE(FindLikelyFiles("ubiquitous", scanner).empty());
}

TEST_F(FileRetrieverTest, FindLikelyFilesStillMatchesDiscriminatingTermsInABigTree) {
    // The saturation rule must not swallow a genuine identifier just
    // because the repository is large.
    for (int i = 0; i < 25; ++i) {
        WriteFile(root_ / "src" / ("Common" + std::to_string(i) + ".cpp"), "int ubiquitous;\n");
    }
    WriteFile(root_ / "src" / "Rare.cpp", "void VeryDistinctiveName() {}\n");
    cppcoder::CodebaseScanner scanner(root_);

    auto likely = FindLikelyFiles("explain VeryDistinctiveName", scanner);
    ASSERT_FALSE(likely.empty());
    EXPECT_EQ(likely[0], "src/Rare.cpp");
}

// ---------------- ParseFileRequests ----------------

TEST(ParseFileRequestsTest, ParsesPlainJson) {
    auto paths = ParseFileRequests(R"({"files": ["src/Alpha.cpp", "src/Beta.cpp"]})");
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(paths[0], "src/Alpha.cpp");
    EXPECT_EQ(paths[1], "src/Beta.cpp");
}

TEST(ParseFileRequestsTest, ParsesMarkdownFencedJson) {
    auto paths = ParseFileRequests("Sure!\n```json\n{\"files\": [\"src/Alpha.cpp\"]}\n```\n");
    ASSERT_EQ(paths.size(), 1u);
    EXPECT_EQ(paths[0], "src/Alpha.cpp");
}

TEST(ParseFileRequestsTest, ParsesProseWrappedJson) {
    auto paths = ParseFileRequests("I think we need: {\"files\": [\"README.md\"]} -- that's all.");
    ASSERT_EQ(paths.size(), 1u);
    EXPECT_EQ(paths[0], "README.md");
}

TEST(ParseFileRequestsTest, EmptyArrayYieldsNoPaths) {
    EXPECT_TRUE(ParseFileRequests(R"({"files": []})").empty());
}

TEST(ParseFileRequestsTest, MissingFilesKeyYieldsNoPaths) {
    EXPECT_TRUE(ParseFileRequests(R"({"answer": "42"})").empty());
}

TEST(ParseFileRequestsTest, NonArrayFilesValueYieldsNoPaths) {
    EXPECT_TRUE(ParseFileRequests(R"({"files": "src/Alpha.cpp"})").empty());
}

TEST(ParseFileRequestsTest, MalformedJsonYieldsNoPaths) {
    EXPECT_TRUE(ParseFileRequests(R"({"files": ["src/Alpha.cpp")").empty());
}

TEST(ParseFileRequestsTest, NoJsonAtAllYieldsNoPaths) {
    EXPECT_TRUE(ParseFileRequests("I'm not sure which files you mean.").empty());
}

TEST(ParseFileRequestsTest, EmptyResponseYieldsNoPaths) {
    EXPECT_TRUE(ParseFileRequests("").empty());
}

TEST(ParseFileRequestsTest, SkipsNonStringAndEmptyEntries) {
    auto paths = ParseFileRequests(R"({"files": ["a.cpp", 7, "", null, "b.cpp"]})");
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(paths[0], "a.cpp");
    EXPECT_EQ(paths[1], "b.cpp");
}

// ---------------- ReadRequestedFiles ----------------

TEST_F(FileRetrieverTest, ReadsRequestedFile) {
    auto files = ReadRequestedFiles({"src/Alpha.cpp"}, root_);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_TRUE(files[0].ok);
    EXPECT_EQ(files[0].path, "src/Alpha.cpp");
    EXPECT_NE(files[0].content.find("int alpha()"), std::string::npos);
    EXPECT_FALSE(files[0].truncated);
}

TEST_F(FileRetrieverTest, ReadsSeveralFiles) {
    auto files = ReadRequestedFiles({"src/Alpha.cpp", "README.md"}, root_);
    ASSERT_EQ(files.size(), 2u);
    EXPECT_TRUE(files[0].ok);
    EXPECT_TRUE(files[1].ok);
}

TEST_F(FileRetrieverTest, RejectsPathEscapingRoot) {
    auto files = ReadRequestedFiles({"../outside.txt"}, root_);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_FALSE(files[0].ok);
    EXPECT_NE(files[0].error.find("outside"), std::string::npos);
}

TEST_F(FileRetrieverTest, RejectsAbsolutePath) {
#ifdef _WIN32
    auto files = ReadRequestedFiles({"C:\\Windows\\win.ini"}, root_);
#else
    auto files = ReadRequestedFiles({"/etc/passwd"}, root_);
#endif
    ASSERT_EQ(files.size(), 1u);
    EXPECT_FALSE(files[0].ok);
    EXPECT_NE(files[0].error.find("outside"), std::string::npos);
}

TEST_F(FileRetrieverTest, ReportsHallucinatedPathRatherThanDroppingIt) {
    auto files = ReadRequestedFiles({"src/DoesNotExist.cpp"}, root_);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_FALSE(files[0].ok);
    EXPECT_EQ(files[0].path, "src/DoesNotExist.cpp");
    EXPECT_FALSE(files[0].error.empty());
}

TEST_F(FileRetrieverTest, DirectoryIsNotAReadableFile) {
    auto files = ReadRequestedFiles({"src"}, root_);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_FALSE(files[0].ok);
}

TEST_F(FileRetrieverTest, TruncatesOversizedFile) {
    WriteFile(root_ / "big.cpp", std::string(cppcoder::kMaxBytesPerRetrievedFile + 5000, 'x'));
    auto files = ReadRequestedFiles({"big.cpp"}, root_);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_TRUE(files[0].ok);
    EXPECT_TRUE(files[0].truncated);
    EXPECT_EQ(files[0].content.size(), cppcoder::kMaxBytesPerRetrievedFile);
}

TEST_F(FileRetrieverTest, CapsTheNumberOfFiles) {
    std::vector<std::string> requested;
    for (std::size_t i = 0; i < cppcoder::kMaxRetrievedFiles + 3; ++i) {
        std::string name = "f" + std::to_string(i) + ".cpp";
        WriteFile(root_ / name, "x\n");
        requested.push_back(name);
    }
    auto files = ReadRequestedFiles(requested, root_);
    EXPECT_EQ(files.size(), cppcoder::kMaxRetrievedFiles);
}

TEST_F(FileRetrieverTest, StopsAtTheTotalByteBudget) {
    // Each file is just under the per-file cap, so the total budget --
    // not the per-file one -- is what has to bite here.
    const std::size_t each = cppcoder::kMaxBytesPerRetrievedFile;
    std::vector<std::string> requested;
    for (std::size_t i = 0; i < cppcoder::kMaxRetrievedFiles; ++i) {
        std::string name = "big" + std::to_string(i) + ".cpp";
        WriteFile(root_ / name, std::string(each, 'x'));
        requested.push_back(name);
    }
    auto files = ReadRequestedFiles(requested, root_);

    std::size_t total = 0;
    for (const auto& f : files) total += f.content.size();
    EXPECT_LE(total, cppcoder::kMaxTotalRetrievedBytes);

    // Anything dropped for budget is reported, not silently omitted.
    bool anyOmitted = false;
    for (const auto& f : files) {
        if (!f.ok) anyOmitted = true;
    }
    EXPECT_TRUE(anyOmitted) << "expected the budget to bite with "
                            << cppcoder::kMaxRetrievedFiles << " files of " << each << " bytes";
}

TEST_F(FileRetrieverTest, EmptyRequestListReadsNothing) {
    EXPECT_TRUE(ReadRequestedFiles({}, root_).empty());
}

// ---------------- FormatFileContext ----------------

TEST(FormatFileContextTest, EmptyInputYieldsEmptyString) {
    EXPECT_TRUE(FormatFileContext({}).empty());
}

TEST(FormatFileContextTest, IncludesPathHeaderAndContent) {
    RetrievedFile f;
    f.path = "src/Alpha.cpp";
    f.content = "int alpha();\n";
    f.ok = true;

    std::string out = FormatFileContext({f});
    EXPECT_NE(out.find("src/Alpha.cpp"), std::string::npos);
    EXPECT_NE(out.find("int alpha();"), std::string::npos);
}

TEST(FormatFileContextTest, MarksTruncatedFiles) {
    RetrievedFile f;
    f.path = "big.cpp";
    f.content = "xxx";
    f.ok = true;
    f.truncated = true;

    EXPECT_NE(FormatFileContext({f}).find("[truncated]"), std::string::npos);
}

TEST(FormatFileContextTest, ListsUnavailableFilesAlongsideReadableOnes) {
    RetrievedFile good;
    good.path = "src/Alpha.cpp";
    good.content = "int alpha();\n";
    good.ok = true;

    RetrievedFile bad;
    bad.path = "src/Ghost.cpp";
    bad.error = "not a readable file";

    std::string out = FormatFileContext({good, bad});
    EXPECT_NE(out.find("src/Ghost.cpp"), std::string::npos);
    EXPECT_NE(out.find("not a readable file"), std::string::npos);
}

TEST(FormatFileContextTest, AllFailedYieldsEmptyStringRatherThanAFailureList) {
    // Spending context telling the model about paths it invented is
    // worse than sending no context at all.
    RetrievedFile bad;
    bad.path = "src/Ghost.cpp";
    bad.error = "not a readable file";

    EXPECT_TRUE(FormatFileContext({bad}).empty());
}

// ---------------- Additional coverage ----------------

TEST_F(FileRetrieverTest, FindLikelyFilesTruncatesToTopMatchesWhenMaxResultsIsSmaller) {
    // TripleMatch hits all three query terms, DoubleMatch hits two, and
    // the pre-existing Alpha.cpp/Beta.cpp fixtures each hit only one --
    // with maxResults capped at 2, only the two strongest should survive,
    // in ranked order.
    WriteFile(root_ / "src" / "TripleMatch.cpp", "alpha beta gamma\n");
    WriteFile(root_ / "src" / "DoubleMatch.cpp", "alpha beta\n");
    cppcoder::CodebaseScanner scanner(root_);

    auto likely = FindLikelyFiles("alpha beta gamma", scanner, 2);
    ASSERT_EQ(likely.size(), 2u);
    EXPECT_EQ(likely[0], "src/TripleMatch.cpp");
    EXPECT_EQ(likely[1], "src/DoubleMatch.cpp");
}

TEST_F(FileRetrieverTest, FindLikelyFilesBreaksTiesByPath) {
    // Both files match exactly one, identical term, so the count-based
    // sort can't order them -- the tiebreak must fall back to a plain
    // alphabetical path comparison.
    WriteFile(root_ / "src" / "Zulu.cpp", "distinctiveterm\n");
    WriteFile(root_ / "src" / "Yankee.cpp", "distinctiveterm\n");
    cppcoder::CodebaseScanner scanner(root_);

    auto likely = FindLikelyFiles("distinctiveterm", scanner);
    ASSERT_EQ(likely.size(), 2u);
    EXPECT_EQ(likely[0], "src/Yankee.cpp");
    EXPECT_EQ(likely[1], "src/Zulu.cpp");
}

TEST_F(FileRetrieverTest, FindLikelyFilesReturnsNothingForMessageWithNoExtractableTerms) {
    // Every word here is either a FallbackKeywords stopword or shorter
    // than its three-character minimum, so zero keywords are extracted
    // and the grep never runs at all -- distinct from the "real keyword
    // that matches nothing" case covered elsewhere in this file.
    cppcoder::CodebaseScanner scanner(root_);
    EXPECT_TRUE(FindLikelyFiles("how is it to do?", scanner).empty());
}

TEST(BuildRetrievalPromptTest, EmptyCandidateListStillProducesAPrompt) {
    std::string prompt = BuildRetrievalPrompt("what does this do?", {});
    EXPECT_NE(prompt.find("what does this do?"), std::string::npos);
    EXPECT_NE(prompt.find("\"files\""), std::string::npos);
}

TEST(BuildRetrievalPromptTest, ListsEveryCandidatePathVerbatim) {
    std::vector<std::string> candidates = {"a/One.cpp", "b/Two.cpp", "c/Three.cpp", "d/Four.cpp"};
    std::string prompt = BuildRetrievalPrompt("question", candidates);
    for (const auto& c : candidates) {
        EXPECT_NE(prompt.find("  " + c + "\n"), std::string::npos) << c;
    }
}

TEST(ParseFileRequestsTest, SkipsBooleanArrayAndObjectEntries) {
    auto paths = ParseFileRequests(R"({"files": ["a.cpp", true, [1,2], {"x":1}, "b.cpp"]})");
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(paths[0], "a.cpp");
    EXPECT_EQ(paths[1], "b.cpp");
}

TEST(ParseFileRequestsTest, NumericFilesValueYieldsNoPaths) {
    EXPECT_TRUE(ParseFileRequests(R"({"files": 7})").empty());
}

TEST(ParseFileRequestsTest, ObjectFilesValueYieldsNoPaths) {
    EXPECT_TRUE(ParseFileRequests(R"({"files": {"path": "src/Alpha.cpp"}})").empty());
}

TEST_F(FileRetrieverTest, OnlyTheFirstMaxFilesAreConsideredWhenOverRequested) {
    // Anything past kMaxRetrievedFiles is never even looked at: it's not
    // present in the result at all, not turned into an ok == false entry.
    std::vector<std::string> requested;
    for (std::size_t i = 0; i < cppcoder::kMaxRetrievedFiles + 2; ++i) {
        std::string name = "f" + std::to_string(i) + ".cpp";
        WriteFile(root_ / name, "x\n");
        requested.push_back(name);
    }

    auto files = ReadRequestedFiles(requested, root_);
    ASSERT_EQ(files.size(), cppcoder::kMaxRetrievedFiles);
    for (std::size_t i = 0; i < files.size(); ++i) {
        EXPECT_EQ(files[i].path, requested[i]);
        EXPECT_TRUE(files[i].ok);
    }
}

TEST_F(FileRetrieverTest, EscapingPathDoesNotPreventValidPathFromSucceeding) {
    auto files = ReadRequestedFiles({"../outside.txt", "src/Alpha.cpp"}, root_);
    ASSERT_EQ(files.size(), 2u);
    EXPECT_FALSE(files[0].ok);
    EXPECT_NE(files[0].error.find("outside"), std::string::npos);
    EXPECT_TRUE(files[1].ok);
    EXPECT_NE(files[1].content.find("int alpha()"), std::string::npos);
}

TEST_F(FileRetrieverTest, FileExactlyAtPerFileCapIsNotTruncatedButOneByteOverIs) {
    WriteFile(root_ / "exact.cpp", std::string(cppcoder::kMaxBytesPerRetrievedFile, 'x'));
    WriteFile(root_ / "over.cpp", std::string(cppcoder::kMaxBytesPerRetrievedFile + 1, 'x'));

    auto files = ReadRequestedFiles({"exact.cpp", "over.cpp"}, root_);
    ASSERT_EQ(files.size(), 2u);

    EXPECT_TRUE(files[0].ok);
    EXPECT_FALSE(files[0].truncated);
    EXPECT_EQ(files[0].content.size(), cppcoder::kMaxBytesPerRetrievedFile);

    EXPECT_TRUE(files[1].ok);
    EXPECT_TRUE(files[1].truncated);
    EXPECT_EQ(files[1].content.size(), cppcoder::kMaxBytesPerRetrievedFile);
}

TEST_F(FileRetrieverTest, DuplicatePathsAreReadIndependently) {
    // No dedup: the same path requested twice is read twice and counts
    // twice against kMaxRetrievedFiles.
    auto files = ReadRequestedFiles({"src/Alpha.cpp", "src/Alpha.cpp"}, root_);
    ASSERT_EQ(files.size(), 2u);
    EXPECT_TRUE(files[0].ok);
    EXPECT_TRUE(files[1].ok);
    EXPECT_EQ(files[0].path, "src/Alpha.cpp");
    EXPECT_EQ(files[1].path, "src/Alpha.cpp");
    EXPECT_EQ(files[0].content, files[1].content);
}

TEST(FormatFileContextTest, MarksEachTruncatedFileIndependently) {
    RetrievedFile first;
    first.path = "src/One.cpp";
    first.content = "one";
    first.ok = true;
    first.truncated = true;

    RetrievedFile second;
    second.path = "src/Two.cpp";
    second.content = "two";
    second.ok = true;
    second.truncated = true;

    std::string out = FormatFileContext({first, second});
    EXPECT_NE(out.find("src/One.cpp"), std::string::npos);
    EXPECT_NE(out.find("src/Two.cpp"), std::string::npos);

    auto firstMarker = out.find("[truncated]");
    ASSERT_NE(firstMarker, std::string::npos);
    auto secondMarker = out.find("[truncated]", firstMarker + 1);
    EXPECT_NE(secondMarker, std::string::npos);
}
