// Live-model tests for Judge::Review against a real, currently-running
// Ollama instance (see tests/OllamaTestSupport.h). Unlike JudgeTests.cpp
// (which drives Judge::ApplyJudgeResponse directly with hand-crafted
// JSON and is fully deterministic/offline), these tests go through the
// real Review() call end-to-end.
//
// The model's actual relevance judgment is non-deterministic, so these
// tests deliberately never assert *which* summary/directions survive.
// They only assert structural invariants that must always hold
// regardless of what the model decides:
//   - Review() never throws/crashes on a well-formed Finding.
//   - areaInvestigated is passed through unchanged (Judge only prunes
//     summary/directions/outcome).
//   - Surviving suggestedDirections are always a subset of the input
//     directions (identified by targetArea) -- Judge cannot invent new
//     directions.
//   - The returned outcome is always one of the three valid values.
//   - Zero input directions can never produce non-zero output directions.

#include "cppcoder/Judge.h"

#include "OllamaTestSupport.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

using cppcoder::Finding;
using cppcoder::Judge;
using cppcoder::OllamaClient;
using cppcoder::Task;
using cppcoder::WorkerOutcome;

namespace {

Task MakeDirection(const std::string& id, const std::string& area) {
    Task t;
    t.id = id;
    t.targetArea = area;
    t.researchGoal = "find out about " + area;
    t.successCriteria = "a short answer about " + area;
    return t;
}

Finding MakeFinding(WorkerOutcome outcome, std::string area, std::string summary,
                     std::vector<Task> directions) {
    Finding f;
    f.outcome = outcome;
    f.areaInvestigated = std::move(area);
    f.summary = std::move(summary);
    f.suggestedDirections = std::move(directions);
    return f;
}

// Every surviving direction's targetArea must appear among the original
// directions' targetArea values -- Judge may only drop or reorder, never
// invent.
void ExpectDirectionsAreSubsetOfOriginal(const std::vector<Task>& result,
                                          const std::vector<Task>& original) {
    std::set<std::string> originalAreas;
    for (const auto& t : original) {
        originalAreas.insert(t.targetArea);
    }
    for (const auto& t : result) {
        EXPECT_TRUE(originalAreas.count(t.targetArea) > 0)
            << "Judge produced a direction targeting '" << t.targetArea
            << "' that was not present in the original suggestedDirections";
    }
}

void ExpectValidOutcome(WorkerOutcome outcome) {
    EXPECT_TRUE(outcome == WorkerOutcome::Success || outcome == WorkerOutcome::NoInformation ||
                outcome == WorkerOutcome::PartialWithDirections);
}

}  // namespace

TEST(JudgeLiveTest, SuccessOutcomeWithShortRelevantSummaryReturnsWellFormedFinding) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    Judge judge(client);

    Finding input =
        MakeFinding(WorkerOutcome::Success, "src/math/Add.cpp",
                    "Add() sums two ints and returns the result.", {});

    Finding result;
    ASSERT_NO_THROW(result = judge.Review("How does integer addition work?", input));

    EXPECT_EQ(result.areaInvestigated, "src/math/Add.cpp");
    ExpectValidOutcome(result.outcome);
    EXPECT_TRUE(result.suggestedDirections.empty());
}

TEST(JudgeLiveTest, PartialWithDirectionsKeepsOnlySubsetOfOriginalDirections) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    Judge judge(client);

    std::vector<Task> directions = {MakeDirection("d0", "src/parser/Lexer.cpp"),
                                     MakeDirection("d1", "src/parser/Parser.cpp"),
                                     MakeDirection("d2", "src/parser/Ast.cpp")};
    Finding input = MakeFinding(WorkerOutcome::PartialWithDirections, "src/parser/Lexer.cpp",
                                 "Tokenizes source text into a stream of tokens.", directions);

    Finding result;
    ASSERT_NO_THROW(result = judge.Review("How does the parser tokenize input?", input));

    EXPECT_EQ(result.areaInvestigated, "src/parser/Lexer.cpp");
    ExpectValidOutcome(result.outcome);
    ExpectDirectionsAreSubsetOfOriginal(result.suggestedDirections, directions);
}

TEST(JudgeLiveTest, NoInformationOutcomeWithEmptyFieldsReturnsWellFormedFinding) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    Judge judge(client);

    Finding input = MakeFinding(WorkerOutcome::NoInformation, "src/unrelated/Empty.cpp", "", {});

    Finding result;
    ASSERT_NO_THROW(result = judge.Review("What does this codebase do?", input));

    EXPECT_EQ(result.areaInvestigated, "src/unrelated/Empty.cpp");
    ExpectValidOutcome(result.outcome);
    EXPECT_TRUE(result.suggestedDirections.empty());
}

TEST(JudgeLiveTest, CompletelyUnrelatedTopicStillReturnsValidFinding) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    Judge judge(client);

    std::vector<Task> directions = {MakeDirection("d0", "src/net/Socket.cpp")};
    Finding input = MakeFinding(WorkerOutcome::PartialWithDirections, "src/net/Socket.cpp",
                                 "Opens a TCP socket and reads bytes off the wire.", directions);

    // The topic here has nothing to do with sockets/networking; this
    // structurally exercises the "prune off-topic content" path without
    // asserting the exact prune decision the model makes.
    Finding result;
    ASSERT_NO_THROW(result = judge.Review("What is the best recipe for banana bread?", input));

    EXPECT_EQ(result.areaInvestigated, "src/net/Socket.cpp");
    ExpectValidOutcome(result.outcome);
    ExpectDirectionsAreSubsetOfOriginal(result.suggestedDirections, directions);
}

TEST(JudgeLiveTest, CalledTwiceInARowWithSameInputsBothReturnIndependentlyValidFindings) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    Judge judge(client);

    std::vector<Task> directions = {MakeDirection("d0", "src/util/Timer.cpp")};
    Finding input = MakeFinding(WorkerOutcome::PartialWithDirections, "src/util/Timer.cpp",
                                 "Timer measures elapsed wall-clock time.", directions);

    Finding firstResult;
    Finding secondResult;
    ASSERT_NO_THROW(firstResult = judge.Review("How is elapsed time measured?", input));
    ASSERT_NO_THROW(secondResult = judge.Review("How is elapsed time measured?", input));

    EXPECT_EQ(firstResult.areaInvestigated, "src/util/Timer.cpp");
    EXPECT_EQ(secondResult.areaInvestigated, "src/util/Timer.cpp");
    ExpectValidOutcome(firstResult.outcome);
    ExpectValidOutcome(secondResult.outcome);
    ExpectDirectionsAreSubsetOfOriginal(firstResult.suggestedDirections, directions);
    ExpectDirectionsAreSubsetOfOriginal(secondResult.suggestedDirections, directions);
}

TEST(JudgeLiveTest, ManySuggestedDirectionsReturnsPromptlyWithSubsetSurviving) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    Judge judge(client);

    std::vector<Task> directions = {
        MakeDirection("d0", "src/a.cpp"), MakeDirection("d1", "src/b.cpp"),
        MakeDirection("d2", "src/c.cpp"), MakeDirection("d3", "src/d.cpp"),
        MakeDirection("d4", "src/e.cpp"), MakeDirection("d5", "src/f.cpp"),
    };
    Finding input = MakeFinding(WorkerOutcome::PartialWithDirections, "src/a.cpp",
                                 "Six small modules, each with one helper function.", directions);

    Finding result;
    ASSERT_NO_THROW(result = judge.Review("What helper functions exist in src/?", input));

    EXPECT_EQ(result.areaInvestigated, "src/a.cpp");
    ExpectValidOutcome(result.outcome);
    ExpectDirectionsAreSubsetOfOriginal(result.suggestedDirections, directions);
}

TEST(JudgeLiveTest, SurvivingDirectionsPreserveTaskFieldsVerbatim) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    Judge judge(client);

    std::vector<Task> directions = {MakeDirection("d0", "src/cache/Lru.cpp")};
    Finding input = MakeFinding(WorkerOutcome::PartialWithDirections, "src/cache/Lru.cpp",
                                 "An LRU cache evicts the least recently used entry.", directions);

    Finding result;
    ASSERT_NO_THROW(result = judge.Review("How does the LRU cache evict entries?", input));

    EXPECT_EQ(result.areaInvestigated, "src/cache/Lru.cpp");
    for (const auto& survivor : result.suggestedDirections) {
        // Find the matching original by id (id is never touched by the
        // judge, so it's a stable key to line survivors back up with).
        bool found = false;
        for (const auto& original : directions) {
            if (original.id != survivor.id) continue;
            found = true;
            EXPECT_EQ(survivor.targetArea, original.targetArea);
            EXPECT_EQ(survivor.researchGoal, original.researchGoal);
            EXPECT_EQ(survivor.successCriteria, original.successCriteria);
        }
        EXPECT_TRUE(found) << "Surviving direction id '" << survivor.id
                            << "' does not match any original direction";
    }
}

TEST(JudgeLiveTest, EmptyMainResearchTopicStillReturnsWellFormedFinding) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    Judge judge(client);

    std::vector<Task> directions = {MakeDirection("d0", "src/io/File.cpp")};
    Finding input = MakeFinding(WorkerOutcome::PartialWithDirections, "src/io/File.cpp",
                                 "File reads bytes from disk into a buffer.", directions);

    Finding result;
    ASSERT_NO_THROW(result = judge.Review("", input));

    EXPECT_EQ(result.areaInvestigated, "src/io/File.cpp");
    ExpectValidOutcome(result.outcome);
    ExpectDirectionsAreSubsetOfOriginal(result.suggestedDirections, directions);
}

TEST(JudgeLiveTest, UnicodeSummaryDoesNotCrashAndReturnsWellFormedFinding) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    Judge judge(client);

    Finding input = MakeFinding(WorkerOutcome::Success, "src/i18n/Format.cpp",
                                 "Formats currency: \xE2\x82\xAC12.50, \xE6\x97\xA5\xE6\x9C\xAC"
                                 "\xE8\xAA\x9E \xF0\x9F\x98\x80 caf\xC3\xA9",
                                 {});

    Finding result;
    ASSERT_NO_THROW(result = judge.Review("How is currency formatted?", input));

    EXPECT_EQ(result.areaInvestigated, "src/i18n/Format.cpp");
    ExpectValidOutcome(result.outcome);
    EXPECT_TRUE(result.suggestedDirections.empty());
}

TEST(JudgeLiveTest, TwoIndependentJudgeInstancesAgainstSameClientBothWorkIndependently) {
    cppcoder_test::SkipUnlessOllamaReady();
    OllamaClient client(cppcoder_test::LiveOllamaConfig());
    Judge judgeOne(client);
    Judge judgeTwo(client);

    std::vector<Task> directionsOne = {MakeDirection("d0", "src/one/A.cpp")};
    std::vector<Task> directionsTwo = {MakeDirection("d0", "src/two/B.cpp")};
    Finding inputOne = MakeFinding(WorkerOutcome::PartialWithDirections, "src/one/A.cpp",
                                    "Module A validates input arguments.", directionsOne);
    Finding inputTwo = MakeFinding(WorkerOutcome::PartialWithDirections, "src/two/B.cpp",
                                    "Module B serializes output to JSON.", directionsTwo);

    Finding resultOne;
    Finding resultTwo;
    ASSERT_NO_THROW(resultOne = judgeOne.Review("How is input validated?", inputOne));
    ASSERT_NO_THROW(resultTwo = judgeTwo.Review("How is output serialized?", inputTwo));

    EXPECT_EQ(resultOne.areaInvestigated, "src/one/A.cpp");
    EXPECT_EQ(resultTwo.areaInvestigated, "src/two/B.cpp");
    ExpectValidOutcome(resultOne.outcome);
    ExpectValidOutcome(resultTwo.outcome);
    ExpectDirectionsAreSubsetOfOriginal(resultOne.suggestedDirections, directionsOne);
    ExpectDirectionsAreSubsetOfOriginal(resultTwo.suggestedDirections, directionsTwo);
}
