// Live-model tests for Editor::Execute against a real, currently-running
// Ollama instance (see tests/OllamaTestSupport.h). Unlike EditorTests.cpp
// (which drives Editor::ParseEditResponse directly with hand-crafted JSON
// and is fully deterministic/offline), these tests go through the real
// Execute() call end-to-end: a real CodebaseScanner over a small temp
// codebase, a real prompt, and a real model response.
//
// A small local model's decision to edit, which outcome it lands on, and
// exactly what content it writes are all non-deterministic, so these
// tests deliberately never assert a specific EditOutcome value or
// specific file content. They only assert structural invariants that
// must always hold regardless of what the model decides:
//   - areaInvestigated equals the task's targetArea exactly.
//   - duration.count() >= 0 and promptTokensApprox is non-negative.
//   - Execute() never throws for a well-formed Task.
//   - Every entry in edits (if any) has a non-empty path -- the same
//     parsing contract already covered offline in EditorTests.cpp.
//   - Every entry in suggestedDirections (if any) has a non-empty
//     targetArea and researchGoal.

#include "cppcoder/Editor.h"

#include "OllamaTestSupport.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace fs = std::filesystem;

using cppcoder::CodebaseScanner;
using cppcoder::EditFinding;
using cppcoder::Editor;
using cppcoder::OllamaClient;
using cppcoder::Task;

namespace {

// Small, clearly-editable temp codebase shared by every test in this
// file. Kept deliberately tiny so each live call stays fast.
class EditorLiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "cppcoder_editor_live_test";
        fs::remove_all(root_);
        fs::create_directories(root_ / "module");

        // A function missing a doc comment -- an easy "add a comment" ask.
        std::ofstream(root_ / "add_comment.cpp")
            << "int Add(int a, int b) {\n"
               "    return a + b;\n"
               "}\n";

        // A function with an obvious bug: Square adds instead of
        // multiplying.
        std::ofstream(root_ / "bug.cpp")
            << "int Square(int x) {\n"
               "    return x + x;  // bug: should be x * x\n"
               "}\n";

        // A function whose documentation requirement is already
        // satisfied -- a plausible (but never asserted) NoChangeNeeded
        // candidate.
        std::ofstream(root_ / "already_documented.cpp")
            << "// Returns the sum of two integers.\n"
               "int Sum(int a, int b) {\n"
               "    return a + b;\n"
               "}\n";

        // A small directory of 2-3 files for area-as-directory tests.
        std::ofstream(root_ / "module" / "a.cpp") << "int GetA() { return 1; }\n";
        std::ofstream(root_ / "module" / "b.cpp") << "int GetB() { return 2; }\n";
        std::ofstream(root_ / "module" / "c.cpp") << "int GetC() { return 3; }\n";

        // A file whose content embeds text that looks like
        // CodebaseScanner's own "// ==== <path> ====" separator, to
        // sanity-check that a real model response involving it doesn't
        // crash the marker-stripping path (Editor.cpp's
        // StripScannerHeaderMarker).
        std::ofstream(root_ / "marker_embedded.cpp")
            << "int Before() { return 1; }\n"
               "\n"
               "// ==== marker_embedded.cpp ====\n"
               "\n"
               "int After() { return 2; }\n";
    }

    void TearDown() override { fs::remove_all(root_); }

    Task MakeTask(const std::string& area, const std::string& goal,
                  const std::string& criteria) const {
        Task task;
        task.id = "editor-live-test";
        task.targetArea = area;
        task.researchGoal = goal;
        task.successCriteria = criteria;
        return task;
    }

    // Invariants that must hold for every EditFinding Execute() ever
    // returns, regardless of what the model decided.
    static void ExpectWellFormedFinding(const EditFinding& finding, const std::string& expectedArea) {
        EXPECT_EQ(finding.areaInvestigated, expectedArea);
        EXPECT_GE(finding.duration.count(), 0);
        EXPECT_GE(finding.promptTokensApprox, 0u);
        for (const auto& edit : finding.edits) {
            EXPECT_FALSE(edit.path.empty());
        }
        for (const auto& dir : finding.suggestedDirections) {
            EXPECT_FALSE(dir.targetArea.empty());
            EXPECT_FALSE(dir.researchGoal.empty());
        }
    }

    fs::path root_;
};

}  // namespace

TEST_F(EditorLiveTest, ExecuteOnSingleSmallFileReturnsWellFormedFinding) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editor(client, scanner);

    Task task = MakeTask("add_comment.cpp", "Add a short comment above the Add function.",
                          "The Add function has a one-line comment describing what it does.");

    EditFinding finding;
    ASSERT_NO_THROW(finding = editor.Execute(task));
    ExpectWellFormedFinding(finding, "add_comment.cpp");
}

TEST_F(EditorLiveTest, ExecuteOnDirectoryWithMultipleFilesReturnsWellFormedFinding) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editor(client, scanner);

    Task task = MakeTask("module", "Add a short comment above each getter function.",
                          "GetA, GetB, and GetC each have a one-line comment.");

    EditFinding finding;
    ASSERT_NO_THROW(finding = editor.Execute(task));
    ExpectWellFormedFinding(finding, "module");
}

TEST_F(EditorLiveTest, ExecuteWithVerySmallTokenBudgetDoesNotCrash) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editor(client, scanner, /*tokenBudget=*/500);

    Task task = MakeTask("bug.cpp", "Fix Square so it returns x * x instead of x + x.",
                          "Square(x) returns x multiplied by itself.");

    EditFinding finding;
    ASSERT_NO_THROW(finding = editor.Execute(task));
    ExpectWellFormedFinding(finding, "bug.cpp");
}

TEST_F(EditorLiveTest, ExecuteTwiceInARowBothIndependentlyValid) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editor(client, scanner);

    Task task = MakeTask("bug.cpp", "Fix Square so it returns x * x instead of x + x.",
                          "Square(x) returns x multiplied by itself.");

    EditFinding first;
    EditFinding second;
    ASSERT_NO_THROW(first = editor.Execute(task));
    ASSERT_NO_THROW(second = editor.Execute(task));
    ExpectWellFormedFinding(first, "bug.cpp");
    ExpectWellFormedFinding(second, "bug.cpp");
}

TEST_F(EditorLiveTest, ExecuteOnAlreadySatisfiedTaskReturnsWellFormedFinding) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editor(client, scanner);

    // Sum already has the requested comment; NoChangeNeeded is plausible
    // here but never asserted.
    Task task = MakeTask("already_documented.cpp",
                          "Ensure the Sum function has a comment describing what it does.",
                          "Sum has a comment above it explaining what it returns.");

    EditFinding finding;
    ASSERT_NO_THROW(finding = editor.Execute(task));
    ExpectWellFormedFinding(finding, "already_documented.cpp");
}

TEST_F(EditorLiveTest, ExecuteOnNonexistentTargetAreaDoesNotThrow) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editor(client, scanner);

    Task task = MakeTask("this/path/does/not/exist.cpp", "Fix whatever is wrong here.",
                          "The problem is fixed.");

    EditFinding finding;
    ASSERT_NO_THROW(finding = editor.Execute(task));
    ExpectWellFormedFinding(finding, "this/path/does/not/exist.cpp");
}

TEST_F(EditorLiveTest, ExecuteWithRepeatableTaskAcrossMultipleFilesReturnsWellFormedFinding) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editor(client, scanner);

    Task task = MakeTask("repeat-root", "Add a short comment above the function in this file.",
                          "The function has a one-line comment describing what it does.");
    task.repeatable = true;
    task.repeatTargets = {"add_comment.cpp", "module/a.cpp", "module/b.cpp"};

    EditFinding finding;
    ASSERT_NO_THROW(finding = editor.Execute(task));
    ExpectWellFormedFinding(finding, "repeat-root");
}

TEST_F(EditorLiveTest, DurationAndTokenCountsAreNonNegativeAcrossDistinctTasks) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editor(client, scanner);

    std::vector<Task> tasks = {
        MakeTask("add_comment.cpp", "Add a comment above Add.", "Add has a comment."),
        MakeTask("bug.cpp", "Fix Square.", "Square(x) returns x * x."),
        MakeTask("module", "Add comments to the getters.", "Each getter has a comment."),
    };

    for (const auto& task : tasks) {
        EditFinding finding;
        ASSERT_NO_THROW(finding = editor.Execute(task));
        EXPECT_GE(finding.duration.count(), 0);
        EXPECT_GE(finding.promptTokensApprox, 0u);
        EXPECT_EQ(finding.areaInvestigated, task.targetArea);
    }
}

TEST_F(EditorLiveTest, ExecuteWithEmptyResearchGoalAndSuccessCriteriaReturnsWellFormedFinding) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editor(client, scanner);

    Task task = MakeTask("add_comment.cpp", "", "");

    EditFinding finding;
    ASSERT_NO_THROW(finding = editor.Execute(task));
    ExpectWellFormedFinding(finding, "add_comment.cpp");
}

TEST_F(EditorLiveTest, TwoIndependentEditorInstancesAgainstSameClientAndScannerBothWork) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editorOne(client, scanner);
    Editor editorTwo(client, scanner);

    Task taskOne = MakeTask("add_comment.cpp", "Add a comment above Add.", "Add has a comment.");
    Task taskTwo = MakeTask("bug.cpp", "Fix Square.", "Square(x) returns x * x.");

    EditFinding resultOne;
    EditFinding resultTwo;
    ASSERT_NO_THROW(resultOne = editorOne.Execute(taskOne));
    ASSERT_NO_THROW(resultTwo = editorTwo.Execute(taskTwo));
    ExpectWellFormedFinding(resultOne, "add_comment.cpp");
    ExpectWellFormedFinding(resultTwo, "bug.cpp");
}

TEST_F(EditorLiveTest, ExecuteWithDefaultTokenBudgetOnSmallCodebaseDoesNotCrash) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    // Default constructor argument: tokenBudget = 120'000.
    Editor editor(client, scanner);

    Task task = MakeTask("", "Add a short comment above any function missing one.",
                          "Functions in this codebase have a one-line comment.");

    EditFinding finding;
    ASSERT_NO_THROW(finding = editor.Execute(task));
    ExpectWellFormedFinding(finding, "");
}

TEST_F(EditorLiveTest, EditsReturnedAlwaysHaveNonEmptyPath) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editor(client, scanner);

    Task task = MakeTask("bug.cpp", "Fix Square so it returns x * x instead of x + x.",
                          "Square(x) returns x multiplied by itself.");

    EditFinding finding;
    ASSERT_NO_THROW(finding = editor.Execute(task));
    for (const auto& edit : finding.edits) {
        EXPECT_FALSE(edit.path.empty())
            << "Editor::ParseEditResponse should have filtered out empty-path edits";
    }
}

TEST_F(EditorLiveTest, SuggestedDirectionsReturnedAlwaysHaveNonEmptyTargetAreaAndGoal) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editor(client, scanner);

    Task task = MakeTask("module", "Add a comment above each getter function.",
                          "Each of GetA, GetB, and GetC has a one-line comment.");

    EditFinding finding;
    ASSERT_NO_THROW(finding = editor.Execute(task));
    for (const auto& direction : finding.suggestedDirections) {
        EXPECT_FALSE(direction.targetArea.empty())
            << "Editor::ParseEditResponse should have filtered out directions missing a target area";
        EXPECT_FALSE(direction.researchGoal.empty())
            << "Editor::ParseEditResponse should have filtered out directions missing a research goal";
    }
}

TEST_F(EditorLiveTest, ExecuteOnTaskWithSpecialCharactersDoesNotCrash) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editor(client, scanner);

    Task task = MakeTask(
        "bug.cpp",
        "Fix Square(): it shouldn't return x+x -- check the \"obvious\" bug! (see ticket #42) "
        "caf\xC3\xA9 \xE2\x80\x94 \xF0\x9F\x98\x80",
        "Square(x) returns x * x; no more x + x anywhere.");

    EditFinding finding;
    ASSERT_NO_THROW(finding = editor.Execute(task));
    ExpectWellFormedFinding(finding, "bug.cpp");
}

TEST_F(EditorLiveTest, ExecuteOnFileWithEmbeddedScannerMarkerTextDoesNotCrash) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Editor editor(client, scanner);

    // marker_embedded.cpp already contains a literal "// ==== ... ===="
    // line in the middle of its content (see SetUp), in addition to the
    // genuine one CodebaseScanner prepends -- this exercises the real
    // model round-trip through StripScannerHeaderMarker without asserting
    // whether stripping actually happened, since that depends on whether
    // the model echoes the marker back.
    Task task = MakeTask("marker_embedded.cpp", "Add a short comment above the Before function.",
                          "Before has a one-line comment describing what it does.");

    EditFinding finding;
    ASSERT_NO_THROW(finding = editor.Execute(task));
    ExpectWellFormedFinding(finding, "marker_embedded.cpp");
}
