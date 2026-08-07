#include "cppcoder/LocalCommands.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

using cppcoder::FindRepoRoot;
using cppcoder::LocalCommandResult;
using cppcoder::TryHandleLocalCommand;

namespace {

class LocalCommandsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "cppcoder_local_commands_test";
        fs::remove_all(root_);
        fs::create_directories(root_ / "sub");
        WriteFile(root_ / "notes.txt", "hello from notes\n");
        WriteFile(root_ / "sub" / "deep.txt", "nested file\n");
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

// ---------------- not a local command ----------------

TEST_F(LocalCommandsTest, PlainMessageIsNotHandled) {
    LocalCommandResult r = TryHandleLocalCommand("hello, what does this function do?", root_);
    EXPECT_FALSE(r.handled);
}

TEST_F(LocalCommandsTest, EmptyMessageIsNotHandled) {
    LocalCommandResult r = TryHandleLocalCommand("", root_);
    EXPECT_FALSE(r.handled);
}

TEST_F(LocalCommandsTest, SlashWithUnknownWordIsNotHandled) {
    LocalCommandResult r = TryHandleLocalCommand("/frobnicate", root_);
    EXPECT_FALSE(r.handled);
}

// ---------------- /pwd, /cwd ----------------

TEST_F(LocalCommandsTest, PwdReturnsRoot) {
    LocalCommandResult r = TryHandleLocalCommand("/pwd", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_EQ(r.text, root_.string());
}

TEST_F(LocalCommandsTest, CwdIsAliasForPwd) {
    LocalCommandResult r = TryHandleLocalCommand("/cwd", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_EQ(r.text, root_.string());
}

TEST_F(LocalCommandsTest, PwdIsCaseInsensitive) {
    LocalCommandResult r = TryHandleLocalCommand("/PWD", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_EQ(r.text, root_.string());
}

// ---------------- /ls ----------------

TEST_F(LocalCommandsTest, LsWithNoArgListsRoot) {
    LocalCommandResult r = TryHandleLocalCommand("/ls", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("notes.txt"), std::string::npos);
    EXPECT_NE(r.text.find("sub/"), std::string::npos);
}

TEST_F(LocalCommandsTest, LsWithArgListsSubdirectory) {
    LocalCommandResult r = TryHandleLocalCommand("/ls sub", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("deep.txt"), std::string::npos);
    EXPECT_EQ(r.text.find("notes.txt"), std::string::npos);
}

TEST_F(LocalCommandsTest, LsRejectsPathEscapingRoot) {
    LocalCommandResult r = TryHandleLocalCommand("/ls ../../etc", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("outside"), std::string::npos);
}

TEST_F(LocalCommandsTest, LsOnFileNotDirectoryReportsError) {
    LocalCommandResult r = TryHandleLocalCommand("/ls notes.txt", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("not a directory"), std::string::npos);
}

TEST_F(LocalCommandsTest, LsOnEmptyDirectoryReportsEmpty) {
    fs::create_directories(root_ / "empty_dir");
    LocalCommandResult r = TryHandleLocalCommand("/ls empty_dir", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("(empty)"), std::string::npos);
}

// ---------------- /read, /cat ----------------

TEST_F(LocalCommandsTest, ReadReturnsFileContent) {
    LocalCommandResult r = TryHandleLocalCommand("/read notes.txt", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("hello from notes"), std::string::npos);
}

TEST_F(LocalCommandsTest, CatIsAliasForRead) {
    LocalCommandResult r = TryHandleLocalCommand("/cat notes.txt", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("hello from notes"), std::string::npos);
}

TEST_F(LocalCommandsTest, ReadNestedPathWorks) {
    LocalCommandResult r = TryHandleLocalCommand("/read sub/deep.txt", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("nested file"), std::string::npos);
}

TEST_F(LocalCommandsTest, ReadMissingPathReportsUsage) {
    LocalCommandResult r = TryHandleLocalCommand("/read", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("missing path"), std::string::npos);
}

TEST_F(LocalCommandsTest, ReadNonexistentFileReportsError) {
    LocalCommandResult r = TryHandleLocalCommand("/read does_not_exist.txt", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("could not open"), std::string::npos);
}

TEST_F(LocalCommandsTest, ReadRejectsPathEscapingRoot) {
    LocalCommandResult r = TryHandleLocalCommand("/read ../outside.txt", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("outside"), std::string::npos);
}

TEST_F(LocalCommandsTest, ReadRejectsAbsolutePath) {
#ifdef _WIN32
    LocalCommandResult r = TryHandleLocalCommand("/read C:\\Windows\\win.ini", root_);
#else
    LocalCommandResult r = TryHandleLocalCommand("/read /etc/passwd", root_);
#endif
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("outside"), std::string::npos);
}

TEST_F(LocalCommandsTest, ReadTruncatesLargeFiles) {
    std::string big(300 * 1024, 'x');  // 300 KB, over the 200 KB cap
    WriteFile(root_ / "big.txt", big);
    LocalCommandResult r = TryHandleLocalCommand("/read big.txt", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("truncated"), std::string::npos);
}

// ---------------- /write ----------------

TEST_F(LocalCommandsTest, WriteCreatesNewFile) {
    LocalCommandResult r = TryHandleLocalCommand("/write new.txt\nsome content", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("wrote"), std::string::npos);
    EXPECT_TRUE(fs::exists(root_ / "new.txt"));
    EXPECT_EQ(ReadFile(root_ / "new.txt"), "some content");
}

TEST_F(LocalCommandsTest, WriteOverwritesExistingFile) {
    LocalCommandResult r = TryHandleLocalCommand("/write notes.txt\nreplaced", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_EQ(ReadFile(root_ / "notes.txt"), "replaced");
}

TEST_F(LocalCommandsTest, WriteCreatesParentDirectories) {
    LocalCommandResult r = TryHandleLocalCommand("/write new/deeper/file.txt\ncontent", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_TRUE(fs::exists(root_ / "new" / "deeper" / "file.txt"));
    EXPECT_EQ(ReadFile(root_ / "new" / "deeper" / "file.txt"), "content");
}

TEST_F(LocalCommandsTest, WriteMissingPathReportsUsage) {
    LocalCommandResult r = TryHandleLocalCommand("/write", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("missing path"), std::string::npos);
}

TEST_F(LocalCommandsTest, WriteWithNoContentWritesEmptyFile) {
    LocalCommandResult r = TryHandleLocalCommand("/write empty.txt", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_TRUE(fs::exists(root_ / "empty.txt"));
    EXPECT_EQ(ReadFile(root_ / "empty.txt"), "");
}

TEST_F(LocalCommandsTest, WriteRejectsPathEscapingRoot) {
    LocalCommandResult r = TryHandleLocalCommand("/write ../outside.txt\ncontent", root_);
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("outside"), std::string::npos);
    EXPECT_FALSE(fs::exists(root_.parent_path() / "outside.txt"));
}

TEST_F(LocalCommandsTest, WriteRejectsAbsolutePath) {
#ifdef _WIN32
    LocalCommandResult r = TryHandleLocalCommand("/write C:\\outside.txt\ncontent", root_);
#else
    LocalCommandResult r = TryHandleLocalCommand("/write /tmp/outside.txt\ncontent", root_);
#endif
    EXPECT_TRUE(r.handled);
    EXPECT_NE(r.text.find("outside"), std::string::npos);
}

TEST_F(LocalCommandsTest, WritePreservesMultilineContent) {
    LocalCommandResult r = TryHandleLocalCommand("/write multi.txt\nline one\nline two\nline three",
                                                  root_);
    EXPECT_TRUE(r.handled);
    EXPECT_EQ(ReadFile(root_ / "multi.txt"), "line one\nline two\nline three");
}

// ---------------- FindRepoRoot ----------------

namespace {

// Lays out, under a plain (non-repo) base directory:
//
//   base/plain/            -- no .git anywhere beneath base
//   base/repo/.git/        -- ordinary clone
//   base/repo/src/deep/
//   base/repo/sub/.git     -- a .git *file*, as a submodule/worktree has
//   base/worktree/.git     -- a .git *file* at its own toplevel
class FindRepoRootTest : public ::testing::Test {
protected:
    void SetUp() override {
        base_ = fs::temp_directory_path() / "cppcoder_find_repo_root_test";
        fs::remove_all(base_);
        fs::create_directories(base_ / "plain" / "nested");
        fs::create_directories(base_ / "repo" / ".git");
        fs::create_directories(base_ / "repo" / "src" / "deep");
        fs::create_directories(base_ / "repo" / "sub");
        WriteGitFile(base_ / "repo" / "sub" / ".git");
        fs::create_directories(base_ / "worktree" / "src");
        WriteGitFile(base_ / "worktree" / ".git");

        // Everything here assumes the system temp directory is not
        // itself inside a checkout. If it is, these expectations are
        // meaningless rather than wrong -- skip instead of failing.
        if (FindRepoRoot(base_ / "plain") != base_ / "plain") {
            GTEST_SKIP() << "system temp directory is inside a git repository";
        }
    }

    void TearDown() override { fs::remove_all(base_); }

    static void WriteGitFile(const fs::path& p) {
        std::ofstream out(p, std::ios::binary);
        out << "gitdir: ../.git/modules/sub\n";
    }

    fs::path base_;
};

}  // namespace

TEST_F(FindRepoRootTest, FindsRootFromTheRootItself) {
    EXPECT_EQ(FindRepoRoot(base_ / "repo"), fs::weakly_canonical(base_ / "repo"));
}

TEST_F(FindRepoRootTest, FindsRootFromNestedSubdirectory) {
    EXPECT_EQ(FindRepoRoot(base_ / "repo" / "src" / "deep"),
              fs::weakly_canonical(base_ / "repo"));
}

TEST_F(FindRepoRootTest, TreatsGitFileAsARoot) {
    EXPECT_EQ(FindRepoRoot(base_ / "worktree" / "src"),
              fs::weakly_canonical(base_ / "worktree"));
}

TEST_F(FindRepoRootTest, NearestEnclosingRepositoryWins) {
    EXPECT_EQ(FindRepoRoot(base_ / "repo" / "sub"), fs::weakly_canonical(base_ / "repo" / "sub"));
}

TEST_F(FindRepoRootTest, ReturnsStartWhenNoRepositoryIsFound) {
    fs::path start = base_ / "plain" / "nested";
    EXPECT_EQ(FindRepoRoot(start), start);
}

TEST_F(FindRepoRootTest, NonexistentPathStillWalksUpToARoot) {
    // weakly_canonical tolerates trailing components that don't exist,
    // so a stale path inside a checkout still resolves to its toplevel.
    EXPECT_EQ(FindRepoRoot(base_ / "repo" / "src" / "gone" / "missing.txt"),
              fs::weakly_canonical(base_ / "repo"));
}

TEST_F(FindRepoRootTest, FilesystemRootTerminatesTheWalk) {
    // Must not loop forever or walk off the end -- "/" (or "C:\") is
    // its own parent on POSIX and empties out on Windows.
    fs::path fsRoot = base_.root_path();
    EXPECT_NO_THROW({ (void)FindRepoRoot(fsRoot); });
}
