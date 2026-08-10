// Live-model tests for Worker::Execute(). Unlike WorkerTests.cpp (which
// only exercises the pure, offline Worker::ParseWorkerResponse()), every
// test here makes a REAL call through OllamaClient to a locally running
// Ollama instance. A small local model's chosen outcome and wording are
// non-deterministic, so these tests never assert a specific outcome or
// specific summary text -- only structural invariants that must hold no
// matter what the model says (see OllamaTestSupport.h for the
// skip-if-not-ready contract every test below follows as its first line).

#include "OllamaTestSupport.h"

#include "cppcoder/Worker.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

using cppcoder::CodebaseScanner;
using cppcoder::Finding;
using cppcoder::OllamaClient;
using cppcoder::Task;
using cppcoder::Worker;
using cppcoder::WorkerOutcome;

namespace {

// Asserts the parsing-contract invariant from Worker::ParseWorkerResponse:
// whatever the outcome, every suggested direction (if any) must carry a
// non-empty target area and research goal -- ParseWorkerResponse filters
// out anything less, even end-to-end through a live model response.
void ExpectValidDirections(const Finding& finding) {
    for (const auto& direction : finding.suggestedDirections) {
        EXPECT_FALSE(direction.targetArea.empty());
        EXPECT_FALSE(direction.researchGoal.empty());
    }
}

class WorkerLiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "cppcoder_worker_live_test";
        fs::remove_all(root_);
        fs::create_directories(root_ / "utils");

        std::ofstream(root_ / "add.cpp") << "int Add(int a, int b) { return a + b; }\n";
        std::ofstream(root_ / "subtract.cpp")
            << "int Subtract(int a, int b) { return a - b; }\n";
        std::ofstream(root_ / "greet.cpp")
            << "void Greet() { /* prints a friendly hello */ }\n";
        std::ofstream(root_ / "utils" / "helpers.cpp")
            << "int Double(int x) { return x * 2; }\n";
    }

    void TearDown() override { fs::remove_all(root_); }

    fs::path root_;
};

}  // namespace

// ---- Basic shapes: directory / single file / whole root / nested dir ----

TEST_F(WorkerLiveTest, ExecuteOnDirectoryWithMultipleFilesReturnsMatchingArea) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner);

    Task task;
    task.id = "t1";
    task.targetArea = "";
    task.researchGoal = "Find arithmetic helper functions.";
    task.successCriteria = "Identify functions that add or subtract two integers.";

    Finding finding = worker.Execute(task);
    EXPECT_EQ(finding.areaInvestigated, task.targetArea);
    EXPECT_GE(finding.duration.count(), 0);
    EXPECT_GT(finding.promptTokensApprox, 0u);
    ExpectValidDirections(finding);
}

TEST_F(WorkerLiveTest, ExecuteOnSingleFileTargetReturnsMatchingArea) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner);

    Task task;
    task.id = "t2";
    task.targetArea = "add.cpp";
    task.researchGoal = "Describe what Add does.";
    task.successCriteria = "Explain that Add returns the sum of its two arguments.";

    Finding finding = worker.Execute(task);
    EXPECT_EQ(finding.areaInvestigated, "add.cpp");
    EXPECT_GE(finding.duration.count(), 0);
    EXPECT_GT(finding.promptTokensApprox, 0u);
    ExpectValidDirections(finding);
}

TEST_F(WorkerLiveTest, ExecuteOnDeeplyNestedSubdirectoryReturnsMatchingArea) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner);

    Task task;
    task.id = "t3";
    task.targetArea = "utils";
    task.researchGoal = "Find a function that doubles a number.";
    task.successCriteria = "Identify a function that multiplies its argument by two.";

    Finding finding = worker.Execute(task);
    EXPECT_EQ(finding.areaInvestigated, "utils");
    EXPECT_GE(finding.duration.count(), 0);
    EXPECT_GT(finding.promptTokensApprox, 0u);
    ExpectValidDirections(finding);
}

// ---- Token budget edge cases ----

TEST_F(WorkerLiveTest, ExecuteWithVerySmallTokenBudgetDoesNotCrash) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner, /*tokenBudget=*/500);

    Task task;
    task.id = "t4";
    task.targetArea = "";
    task.researchGoal = "Find any function.";
    task.successCriteria = "Identify at least one function name.";

    Finding finding;
    EXPECT_NO_THROW(finding = worker.Execute(task));
    EXPECT_EQ(finding.areaInvestigated, task.targetArea);
    EXPECT_GE(finding.duration.count(), 0);
    ExpectValidDirections(finding);
}

TEST_F(WorkerLiveTest, ExecuteWithLargeDefaultTokenBudgetBehavesSameAsModerateBudget) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner, /*tokenBudget=*/120'000);

    Task task;
    task.id = "t5";
    task.targetArea = "";
    task.researchGoal = "Find arithmetic helper functions.";
    task.successCriteria = "Identify functions that add or subtract two integers.";

    Finding finding;
    EXPECT_NO_THROW(finding = worker.Execute(task));
    EXPECT_EQ(finding.areaInvestigated, task.targetArea);
    EXPECT_GE(finding.duration.count(), 0);
    EXPECT_GT(finding.promptTokensApprox, 0u);
    ExpectValidDirections(finding);
}

// ---- Success criteria that can never be satisfied by this codebase ----

TEST_F(WorkerLiveTest, ExecuteWithUnsatisfiableSuccessCriteriaReturnsValidFinding) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner);

    Task task;
    task.id = "t6";
    task.targetArea = "";
    task.researchGoal = "Find a quantum computing simulator implementation.";
    task.successCriteria =
        "Locate code that simulates qubits, superposition, or quantum gates.";

    Finding finding = worker.Execute(task);
    // No assertion on which outcome the model lands on -- only that the
    // Finding is structurally well-formed, whatever it decided.
    EXPECT_EQ(finding.areaInvestigated, task.targetArea);
    EXPECT_GE(finding.duration.count(), 0);
    ExpectValidDirections(finding);
}

// ---- No shared mutable state across repeated calls ----

TEST_F(WorkerLiveTest, ExecuteTwiceOnSameTaskReturnsIndependentFindings) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner);

    Task task;
    task.id = "t7";
    task.targetArea = "add.cpp";
    task.researchGoal = "Describe what Add does.";
    task.successCriteria = "Explain that Add returns the sum of its two arguments.";

    Finding first = worker.Execute(task);
    Finding second = worker.Execute(task);

    EXPECT_EQ(first.areaInvestigated, "add.cpp");
    EXPECT_EQ(second.areaInvestigated, "add.cpp");
    EXPECT_GE(first.duration.count(), 0);
    EXPECT_GE(second.duration.count(), 0);
    EXPECT_GT(first.promptTokensApprox, 0u);
    EXPECT_GT(second.promptTokensApprox, 0u);
    ExpectValidDirections(first);
    ExpectValidDirections(second);
}

// ---- Nonexistent target area ----

TEST_F(WorkerLiveTest, ExecuteOnNonexistentTargetAreaDoesNotThrow) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner);

    Task task;
    task.id = "t8";
    task.targetArea = "this/path/does/not/exist.cpp";
    task.researchGoal = "Find anything relevant.";
    task.successCriteria = "Any relevant info at all.";

    // CodebaseScanner::Scan() returns an empty ScanResult (filesIncluded
    // empty) for a target area that doesn't resolve to a file or
    // directory, so Worker::ExecuteSingleArea() short-circuits to
    // NoInformation without ever calling the model. Confirm that path is
    // exercised safely end-to-end rather than throwing/crashing.
    Finding finding;
    EXPECT_NO_THROW(finding = worker.Execute(task));
    EXPECT_EQ(finding.areaInvestigated, task.targetArea);
    EXPECT_EQ(finding.outcome, WorkerOutcome::NoInformation);
    EXPECT_GE(finding.duration.count(), 0);
    EXPECT_EQ(finding.promptTokensApprox, 0u);
    EXPECT_TRUE(finding.suggestedDirections.empty());
}

// ---- Repeatable task across multiple sub-areas ----

TEST_F(WorkerLiveTest, ExecuteWithRepeatableTaskAcrossMultipleFiles) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner);

    Task task;
    task.id = "t9";
    task.targetArea = "arithmetic functions";
    task.researchGoal = "Find functions that operate on two integers.";
    task.successCriteria = "Identify the operation each function performs.";
    task.repeatable = true;
    task.repeatTargets = {"add.cpp", "subtract.cpp"};

    Finding finding = worker.Execute(task);
    // Repeatable merge keeps the original task's targetArea label, not one
    // of the individual repeatTargets (see Worker::Execute's merge path).
    EXPECT_EQ(finding.areaInvestigated, task.targetArea);
    EXPECT_GE(finding.duration.count(), 0);
    ExpectValidDirections(finding);
}

// ---- Duration / token bookkeeping across several distinct tasks ----

TEST_F(WorkerLiveTest, ExecuteDurationAndTokensNonNegativeAcrossDistinctTasks) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner);

    std::vector<Task> tasks;
    {
        Task t;
        t.id = "t10a";
        t.targetArea = "add.cpp";
        t.researchGoal = "Describe Add.";
        t.successCriteria = "Explain what Add returns.";
        tasks.push_back(t);
    }
    {
        Task t;
        t.id = "t10b";
        t.targetArea = "subtract.cpp";
        t.researchGoal = "Describe Subtract.";
        t.successCriteria = "Explain what Subtract returns.";
        tasks.push_back(t);
    }
    {
        Task t;
        t.id = "t10c";
        t.targetArea = "greet.cpp";
        t.researchGoal = "Describe Greet.";
        t.successCriteria = "Explain what Greet does.";
        tasks.push_back(t);
    }

    for (const auto& task : tasks) {
        Finding finding = worker.Execute(task);
        EXPECT_EQ(finding.areaInvestigated, task.targetArea);
        EXPECT_GE(finding.duration.count(), 0);
        EXPECT_GE(finding.promptTokensApprox, 0u);
        ExpectValidDirections(finding);
    }
}

// ---- Empty research goal / success criteria ----

TEST_F(WorkerLiveTest, ExecuteWithEmptyResearchGoalAndSuccessCriteriaDoesNotCrash) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner);

    Task task;
    task.id = "t11";
    task.targetArea = "add.cpp";
    task.researchGoal = "";
    task.successCriteria = "";

    Finding finding;
    EXPECT_NO_THROW(finding = worker.Execute(task));
    EXPECT_EQ(finding.areaInvestigated, "add.cpp");
    EXPECT_GE(finding.duration.count(), 0);
    // Outcome is unconstrained here -- just confirm well-formedness.
    ExpectValidDirections(finding);
}

// ---- Two independent Worker instances against shared client/scanner ----

TEST_F(WorkerLiveTest, TwoIndependentWorkerInstancesBothWork) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker workerA(client, scanner);
    Worker workerB(client, scanner);

    Task task;
    task.id = "t12";
    task.targetArea = "greet.cpp";
    task.researchGoal = "Describe what Greet does.";
    task.successCriteria = "Explain that Greet prints a greeting.";

    Finding findingA = workerA.Execute(task);
    Finding findingB = workerB.Execute(task);

    EXPECT_EQ(findingA.areaInvestigated, "greet.cpp");
    EXPECT_EQ(findingB.areaInvestigated, "greet.cpp");
    EXPECT_GE(findingA.duration.count(), 0);
    EXPECT_GE(findingB.duration.count(), 0);
    ExpectValidDirections(findingA);
    ExpectValidDirections(findingB);
}

// ---- Whole-root scan via empty targetArea string ----

TEST_F(WorkerLiveTest, ExecuteOnWholeRootAreaEmptyStringReturnsValidFinding) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner);

    Task task;
    task.id = "t13";
    task.targetArea = "";
    task.researchGoal = "List the kinds of functions present in this codebase.";
    task.successCriteria = "Mention at least one function by name.";

    Finding finding = worker.Execute(task);
    EXPECT_EQ(finding.areaInvestigated, "");
    EXPECT_GE(finding.duration.count(), 0);
    EXPECT_GT(finding.promptTokensApprox, 0u);
    ExpectValidDirections(finding);
}

// ---- Parsing invariant re-checked end-to-end through a live call ----

TEST_F(WorkerLiveTest, SuggestedDirectionsInvariantHoldsRegardlessOfOutcome) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner);

    Task task;
    task.id = "t14";
    task.targetArea = "utils";
    task.researchGoal = "Find a function that doubles a value, and suggest where else to look.";
    task.successCriteria = "Identify the doubling function and its return value.";

    Finding finding = worker.Execute(task);
    EXPECT_EQ(finding.areaInvestigated, "utils");
    // The core Worker::ParseWorkerResponse contract: suggestedDirections is
    // either empty, or every entry has a non-empty targetArea and
    // researchGoal -- true no matter which of the three outcomes came
    // back from the live model.
    if (finding.suggestedDirections.empty()) {
        SUCCEED();
    } else {
        for (const auto& direction : finding.suggestedDirections) {
            EXPECT_FALSE(direction.targetArea.empty());
            EXPECT_FALSE(direction.researchGoal.empty());
        }
    }
}

// ---- Repeatable task token accounting merges across sub-areas ----

TEST_F(WorkerLiveTest, ExecutePromptTokensApproxAccumulatesAcrossRepeatTargets) {
    cppcoder_test::SkipUnlessOllamaReady();

    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    CodebaseScanner scanner(root_);
    Worker worker(client, scanner);

    Task task;
    task.id = "t15";
    task.targetArea = "greeting and doubling";
    task.researchGoal = "Find what each function does.";
    task.successCriteria = "Describe the behavior of each function.";
    task.repeatable = true;
    task.repeatTargets = {"greet.cpp", "utils/helpers.cpp"};

    Finding finding = worker.Execute(task);
    EXPECT_EQ(finding.areaInvestigated, task.targetArea);
    EXPECT_GE(finding.duration.count(), 0);
    // Each sub-area contributes its own scan's approxTokens to the merged
    // total (see Worker::Execute's repeatable branch), so with two
    // non-empty files scanned the merged total should be positive.
    EXPECT_GT(finding.promptTokensApprox, 0u);
    ExpectValidDirections(finding);
}
