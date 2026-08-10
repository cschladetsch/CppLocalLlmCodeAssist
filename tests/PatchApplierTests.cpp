#include "cppcoder/PatchApplier.h"
#include "cppcoder/Types.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

class PatchApplierTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "cppcoder_patchapplier_test";
        fs::remove_all(root_);
        fs::create_directories(root_);
        WriteFile(root_ / "existing.cpp", "int Original() { return 1; }\n");
    }

    void TearDown() override { fs::remove_all(root_); }

    static void WriteFile(const fs::path& p, const std::string& content) {
        std::ofstream out(p, std::ios::binary);
        out << content;
    }

    static std::string ReadFile(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    fs::path root_;
};

}  // namespace

TEST_F(PatchApplierTest, OverwritesExistingFileContent) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
    edit.path = "existing.cpp";
    edit.newContent = "int Changed() { return 2; }\n";

    auto outcome = applier.Apply({edit});
    ASSERT_EQ(outcome.writtenPaths.size(), 1u);
    EXPECT_EQ(outcome.writtenPaths[0], "existing.cpp");
    EXPECT_TRUE(outcome.rejectedPaths.empty());
    EXPECT_TRUE(outcome.errors.empty());
    EXPECT_EQ(ReadFile(root_ / "existing.cpp"), "int Changed() { return 2; }\n");
}

TEST_F(PatchApplierTest, CreatesNewFileAndParentDirectories) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
    edit.path = "sub/deeper/new_file.cpp";
    edit.newContent = "void NewFunction() {}\n";

    auto outcome = applier.Apply({edit});
    ASSERT_EQ(outcome.writtenPaths.size(), 1u);
    EXPECT_TRUE(fs::exists(root_ / "sub" / "deeper" / "new_file.cpp"));
    EXPECT_EQ(ReadFile(root_ / "sub" / "deeper" / "new_file.cpp"), "void NewFunction() {}\n");
}

TEST_F(PatchApplierTest, RejectsPathTraversalOutsideRoot) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
    edit.path = "../outside.cpp";
    edit.newContent = "should never be written";

    auto outcome = applier.Apply({edit});
    EXPECT_TRUE(outcome.writtenPaths.empty());
    ASSERT_EQ(outcome.rejectedPaths.size(), 1u);
    EXPECT_EQ(outcome.rejectedPaths[0], "../outside.cpp");
    EXPECT_FALSE(fs::exists(root_.parent_path() / "outside.cpp"));
}

TEST_F(PatchApplierTest, RejectsDeeperPathTraversalOutsideRoot) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
    edit.path = "sub/../../escaped.cpp";
    edit.newContent = "should never be written";

    auto outcome = applier.Apply({edit});
    EXPECT_TRUE(outcome.writtenPaths.empty());
    ASSERT_EQ(outcome.rejectedPaths.size(), 1u);
}

TEST_F(PatchApplierTest, RejectsAbsolutePath) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
#ifdef _WIN32
    edit.path = (root_.root_name().string()) + "\\outside_via_absolute.cpp";
#else
    edit.path = "/tmp/outside_via_absolute.cpp";
#endif
    edit.newContent = "should never be written";

    auto outcome = applier.Apply({edit});
    EXPECT_TRUE(outcome.writtenPaths.empty());
    ASSERT_EQ(outcome.rejectedPaths.size(), 1u);
}

TEST_F(PatchApplierTest, RejectsEmptyPath) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
    edit.path = "";
    edit.newContent = "irrelevant";

    auto outcome = applier.Apply({edit});
    EXPECT_TRUE(outcome.writtenPaths.empty());
    ASSERT_EQ(outcome.rejectedPaths.size(), 1u);
}

TEST_F(PatchApplierTest, AppliesMultipleEditsInOneCall) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit a;
    a.path = "a.cpp";
    a.newContent = "// a\n";
    cppcoder::ProposedEdit b;
    b.path = "b.cpp";
    b.newContent = "// b\n";

    auto outcome = applier.Apply({a, b});
    EXPECT_EQ(outcome.writtenPaths.size(), 2u);
    EXPECT_EQ(ReadFile(root_ / "a.cpp"), "// a\n");
    EXPECT_EQ(ReadFile(root_ / "b.cpp"), "// b\n");
}

TEST_F(PatchApplierTest, EmptyEditsListProducesEmptyOutcome) {
    cppcoder::PatchApplier applier(root_);
    auto outcome = applier.Apply({});
    EXPECT_TRUE(outcome.writtenPaths.empty());
    EXPECT_TRUE(outcome.rejectedPaths.empty());
    EXPECT_TRUE(outcome.errors.empty());
}

TEST_F(PatchApplierTest, OverwriteTruncatesLongerExistingContent) {
    WriteFile(root_ / "shrink.cpp",
              "// this file starts out with a lot more content than it will end up with\n"
              "int LongOriginalFunction(int a, int b, int c) { return a + b + c; }\n");

    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
    edit.path = "shrink.cpp";
    edit.newContent = "// short\n";

    auto outcome = applier.Apply({edit});
    ASSERT_EQ(outcome.writtenPaths.size(), 1u);
    EXPECT_EQ(ReadFile(root_ / "shrink.cpp"), "// short\n");
    EXPECT_EQ(fs::file_size(root_ / "shrink.cpp"), edit.newContent.size());
}

TEST_F(PatchApplierTest, CreatesDeeplyNestedParentDirectories) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
    edit.path = "a/b/c/d/e/deep_file.cpp";
    edit.newContent = "// deeply nested\n";

    auto outcome = applier.Apply({edit});
    ASSERT_EQ(outcome.writtenPaths.size(), 1u);
    EXPECT_TRUE(fs::is_directory(root_ / "a"));
    EXPECT_TRUE(fs::is_directory(root_ / "a" / "b" / "c" / "d" / "e"));
    EXPECT_EQ(ReadFile(root_ / "a" / "b" / "c" / "d" / "e" / "deep_file.cpp"),
              "// deeply nested\n");
}

TEST_F(PatchApplierTest, AcceptsMixedForwardAndBackslashSeparators) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
#ifdef _WIN32
    edit.path = "mixdir\\sub/mixed_file.cpp";
#else
    edit.path = "mixdir/mixed_file.cpp";
#endif
    edit.newContent = "// mixed separators\n";

    auto outcome = applier.Apply({edit});
    ASSERT_EQ(outcome.writtenPaths.size(), 1u);
    EXPECT_TRUE(outcome.rejectedPaths.empty());
#ifdef _WIN32
    EXPECT_EQ(ReadFile(root_ / "mixdir" / "sub" / "mixed_file.cpp"), "// mixed separators\n");
#else
    EXPECT_EQ(ReadFile(root_ / "mixdir" / "mixed_file.cpp"), "// mixed separators\n");
#endif
}

TEST_F(PatchApplierTest, SafeEditWrittenWhileSiblingTraversalEditRejectedInSameBatch) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit unsafe;
    unsafe.path = "../escape_batch.cpp";
    unsafe.newContent = "should never be written";
    cppcoder::ProposedEdit safe;
    safe.path = "safe.cpp";
    safe.newContent = "// safe\n";

    auto outcome = applier.Apply({unsafe, safe});
    ASSERT_EQ(outcome.rejectedPaths.size(), 1u);
    EXPECT_EQ(outcome.rejectedPaths[0], "../escape_batch.cpp");
    ASSERT_EQ(outcome.writtenPaths.size(), 1u);
    EXPECT_EQ(outcome.writtenPaths[0], "safe.cpp");
    EXPECT_EQ(ReadFile(root_ / "safe.cpp"), "// safe\n");
    EXPECT_FALSE(fs::exists(root_.parent_path() / "escape_batch.cpp"));
}

TEST_F(PatchApplierTest, EmptyNewContentWritesZeroByteFile) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
    edit.path = "empty_file.cpp";
    edit.newContent = "";

    auto outcome = applier.Apply({edit});
    ASSERT_EQ(outcome.writtenPaths.size(), 1u);
    EXPECT_TRUE(outcome.errors.empty());
    ASSERT_TRUE(fs::exists(root_ / "empty_file.cpp"));
    EXPECT_EQ(fs::file_size(root_ / "empty_file.cpp"), 0u);
}

TEST_F(PatchApplierTest, SamePathTargetedTwiceLastEditWins) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit first;
    first.path = "dup.cpp";
    first.newContent = "// first version\n";
    cppcoder::ProposedEdit second;
    second.path = "dup.cpp";
    second.newContent = "// second version\n";

    auto outcome = applier.Apply({first, second});
    ASSERT_EQ(outcome.writtenPaths.size(), 2u);
    EXPECT_EQ(outcome.writtenPaths[0], "dup.cpp");
    EXPECT_EQ(outcome.writtenPaths[1], "dup.cpp");
    EXPECT_EQ(ReadFile(root_ / "dup.cpp"), "// second version\n");
}

TEST_F(PatchApplierTest, DotSegmentInPathResolvesNormally) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
    edit.path = "./sub/dotfile.cpp";
    edit.newContent = "// via dot segment\n";

    auto outcome = applier.Apply({edit});
    ASSERT_EQ(outcome.writtenPaths.size(), 1u);
    EXPECT_EQ(outcome.writtenPaths[0], "./sub/dotfile.cpp");
    EXPECT_TRUE(outcome.rejectedPaths.empty());
    EXPECT_EQ(ReadFile(root_ / "sub" / "dotfile.cpp"), "// via dot segment\n");
}

TEST_F(PatchApplierTest, RejectsSymlinkEscapingRoot) {
    fs::path outside = fs::temp_directory_path() / "cppcoder_patchapplier_outside";
    fs::remove_all(outside);
    fs::create_directories(outside);

    fs::path link = root_ / "linkdir";
    std::error_code ec;
    fs::create_directory_symlink(outside, link, ec);
    if (ec) {
        fs::remove_all(outside);
        GTEST_SKIP() << "symlink creation not permitted in this environment: " << ec.message();
    }

    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
    edit.path = "linkdir/escaped_via_symlink.cpp";
    edit.newContent = "should never land outside root";

    auto outcome = applier.Apply({edit});
    EXPECT_TRUE(outcome.writtenPaths.empty());
    ASSERT_EQ(outcome.rejectedPaths.size(), 1u);
    EXPECT_EQ(outcome.rejectedPaths[0], "linkdir/escaped_via_symlink.cpp");
    EXPECT_FALSE(fs::exists(outside / "escaped_via_symlink.cpp"));

    fs::remove_all(outside);
}

TEST_F(PatchApplierTest, RejectsMultiSegmentDotDotTraversal) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
    edit.path = "a/../../../etc/passwd";
    edit.newContent = "should never be written";

    auto outcome = applier.Apply({edit});
    EXPECT_TRUE(outcome.writtenPaths.empty());
    ASSERT_EQ(outcome.rejectedPaths.size(), 1u);
    EXPECT_EQ(outcome.rejectedPaths[0], "a/../../../etc/passwd");
}

TEST_F(PatchApplierTest, RejectsWindowsUncPath) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
#ifdef _WIN32
    edit.path = "\\\\server\\share\\outside.cpp";
#else
    edit.path = "//server/share/outside.cpp";
#endif
    edit.newContent = "should never be written";

    auto outcome = applier.Apply({edit});
    EXPECT_TRUE(outcome.writtenPaths.empty());
    ASSERT_EQ(outcome.rejectedPaths.size(), 1u);
    EXPECT_EQ(outcome.rejectedPaths[0], edit.path);
}

TEST_F(PatchApplierTest, DescriptionFieldDoesNotAffectWrittenContent) {
    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
    edit.path = "described.cpp";
    edit.newContent = "// actual content\n";
    edit.description = "This description text must never end up in the file on disk";

    auto outcome = applier.Apply({edit});
    ASSERT_EQ(outcome.writtenPaths.size(), 1u);
    EXPECT_EQ(ReadFile(root_ / "described.cpp"), "// actual content\n");
}

TEST_F(PatchApplierTest, LargeBatchWrittenInGivenOrder) {
    cppcoder::PatchApplier applier(root_);
    std::vector<cppcoder::ProposedEdit> edits;
    constexpr int kCount = 12;
    for (int i = 0; i < kCount; ++i) {
        cppcoder::ProposedEdit edit;
        edit.path = "batch/file_" + std::to_string(i) + ".cpp";
        edit.newContent = "// content " + std::to_string(i) + "\n";
        edits.push_back(edit);
    }

    auto outcome = applier.Apply(edits);
    ASSERT_EQ(outcome.writtenPaths.size(), static_cast<size_t>(kCount));
    EXPECT_TRUE(outcome.rejectedPaths.empty());
    EXPECT_TRUE(outcome.errors.empty());
    for (int i = 0; i < kCount; ++i) {
        EXPECT_EQ(outcome.writtenPaths[static_cast<size_t>(i)], edits[static_cast<size_t>(i)].path);
        EXPECT_EQ(ReadFile(root_ / "batch" / ("file_" + std::to_string(i) + ".cpp")),
                  "// content " + std::to_string(i) + "\n");
    }
}

TEST_F(PatchApplierTest, WritesBinaryContentWithEmbeddedNullBytes) {
    const char raw[] = {'B', 'I', 'N', '\0', 0x01, 0x02, 'X', '\0', 'Y'};
    std::string content(raw, sizeof(raw));

    cppcoder::PatchApplier applier(root_);
    cppcoder::ProposedEdit edit;
    edit.path = "binary.dat";
    edit.newContent = content;

    auto outcome = applier.Apply({edit});
    ASSERT_EQ(outcome.writtenPaths.size(), 1u);
    ASSERT_EQ(fs::file_size(root_ / "binary.dat"), content.size());
    EXPECT_EQ(ReadFile(root_ / "binary.dat"), content);
}
