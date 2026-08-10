// Live-Ollama integration tests for ResearchEngine::Research(). Unlike
// tests/ResearchEngineTests.cpp (pure, offline, exercises FallbackKeywords
// and SeedInitialTasks directly), every test here drives the *full*
// worker -> judge -> queue loop against a real, currently-running Ollama
// instance. See tests/OllamaTestSupport.h for why every test starts with
// SkipUnlessOllamaReady() and skips (never fails red) when Ollama isn't
// there.
//
// Runtime discipline: this is the most expensive file in the live-test
// batch, since Research() can issue many sequential model calls per run
// (keyword extraction, then worker+judge per iteration, then answer
// synthesis). Every EngineConfig used below caps maxIterations to 0-3 and
// maxWallClock to one minute, and the temp codebase is a handful of
// one-line files so every prompt built from it stays tiny.
//
// Because a small local model's outputs are non-deterministic, none of
// these tests assert specific answer text, specific iteration counts, or
// specific termination-reason strings -- only the structural invariants
// that must hold for *any* outcome (see ExpectWellFormedResult below,
// which is derived directly from reading src/ResearchEngine.cpp: when
// `answered` is false, `answer` and `successfulFindings` are left at
// their default-constructed empty state on every return path).

#include "cppcoder/ResearchEngine.h"

#include "OllamaTestSupport.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

class ResearchEngineLiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "cppcoder_research_engine_live_test";
        fs::remove_all(root_);
        fs::create_directories(root_);
        std::ofstream(root_ / "crypto.cpp") << "int DeriveKey() { return 0; }\n";
        std::ofstream(root_ / "parser.cpp") << "void ParseDocument() {}\n";
        std::ofstream(root_ / "widget.cpp") << "void RenderWidget() {}\n";
    }

    void TearDown() override { fs::remove_all(root_); }

    // Every config used in this file funnels through here so maxWallClock
    // is always capped, regardless of which test constructs it.
    cppcoder::EngineConfig MakeConfig(int maxIterations, std::size_t maxInitialKeywords = 5,
                                       std::size_t tokenBudgetPerTask = 120'000) const {
        cppcoder::EngineConfig config;
        config.tokenBudgetPerTask = tokenBudgetPerTask;
        config.maxIterations = maxIterations;
        config.maxWallClock = std::chrono::minutes(1);
        config.maxInitialKeywords = maxInitialKeywords;
        return config;
    }

    cppcoder::ResearchEngine MakeEngine(cppcoder::EngineConfig config) const {
        return cppcoder::ResearchEngine(cppcoder::OllamaClient(cppcoder_test::LiveOllamaConfig()),
                                         cppcoder::CodebaseScanner(root_), config);
    }

    // A question whose literal identifier ("DeriveKey") appears verbatim
    // in crypto.cpp's content, so FindFilesMatchingKeyword matches it
    // regardless of whether the model's own keyword extraction succeeds
    // or ExtractKeywords falls back to FallbackKeywords() -- both paths
    // are very likely to surface "DeriveKey" as a keyword since it's the
    // only identifier-shaped term in the question.
    static const std::string& MatchingQuestion() {
        static const std::string q = "What does DeriveKey do in crypto.cpp?";
        return q;
    }

    // Invented, codebase-unrelated terms that should not appear in any of
    // the three tiny temp files above (by content or filename).
    static const std::string& NonMatchingQuestion() {
        static const std::string q = "Describe the ZyxvqFlaggleTorbenator interplanetary gizmo";
        return q;
    }

    fs::path root_;
};

// Structural invariants that must hold for ResearchResult regardless of
// the (non-deterministic) outcome. Derived from reading Research()'s
// three return paths in src/ResearchEngine.cpp: the early-return when
// SeedInitialTasks finds nothing, the loop-exhausted-without-success
// path, and the successful-synthesis path.
void ExpectWellFormedResult(const cppcoder::ResearchResult& result,
                             const cppcoder::EngineConfig& config) {
    EXPECT_GE(result.iterationsRun, 0);
    EXPECT_LE(result.iterationsRun, config.maxIterations);
    EXPECT_GE(result.wallClock.count(), 0);
    // Loose sanity ceiling, not a timing assertion: guards against a
    // runaway call silently "passing" rather than being flagged as slow.
    EXPECT_LT(result.wallClock, std::chrono::minutes(5));
    EXPECT_FALSE(result.terminationReason.empty());
    if (result.answered) {
        EXPECT_FALSE(result.answer.empty());
        EXPECT_FALSE(result.successfulFindings.empty());
    } else {
        EXPECT_TRUE(result.answer.empty());
        EXPECT_TRUE(result.successfulFindings.empty());
    }
}

}  // namespace

TEST_F(ResearchEngineLiveTest, MaxIterationsOneOnMatchingQuestionReturnsWellFormedResult) {
    cppcoder_test::SkipUnlessOllamaReady();

    auto config = MakeConfig(/*maxIterations=*/1);
    auto engine = MakeEngine(config);

    cppcoder::ResearchResult result;
    ASSERT_NO_THROW(result = engine.Research(MatchingQuestion()));
    ExpectWellFormedResult(result, config);
}

TEST_F(ResearchEngineLiveTest, MaxIterationsTwoOnMatchingQuestionReturnsWellFormedResult) {
    cppcoder_test::SkipUnlessOllamaReady();

    auto config = MakeConfig(/*maxIterations=*/2);
    auto engine = MakeEngine(config);

    cppcoder::ResearchResult result;
    ASSERT_NO_THROW(result = engine.Research(MatchingQuestion()));
    ExpectWellFormedResult(result, config);
}

TEST_F(ResearchEngineLiveTest, QuestionWithNoCodebaseMatchesStillReturnsWellFormedResult) {
    cppcoder_test::SkipUnlessOllamaReady();

    // If SeedInitialTasks truly finds nothing, Research() short-circuits
    // immediately with answered=false and a keyword-related
    // terminationReason (see the early-return branch in
    // src/ResearchEngine.cpp). We don't assert that branch was taken --
    // the model-driven keyword extraction could in principle surface
    // something unexpected -- only that whichever path ran, the result
    // is still well-formed.
    auto config = MakeConfig(/*maxIterations=*/2);
    auto engine = MakeEngine(config);

    cppcoder::ResearchResult result;
    ASSERT_NO_THROW(result = engine.Research(NonMatchingQuestion()));
    ExpectWellFormedResult(result, config);
}

TEST_F(ResearchEngineLiveTest, IterationsRunNeverExceedsConfiguredMaxAcrossThreeConfigs) {
    cppcoder_test::SkipUnlessOllamaReady();

    for (int maxIterations : {1, 2, 3}) {
        auto config = MakeConfig(maxIterations);
        auto engine = MakeEngine(config);

        cppcoder::ResearchResult result;
        ASSERT_NO_THROW(result = engine.Research(MatchingQuestion()));
        ExpectWellFormedResult(result, config);
    }
}

TEST_F(ResearchEngineLiveTest, EventSinkCollectsAtLeastOneEventDuringRun) {
    cppcoder_test::SkipUnlessOllamaReady();

    auto config = MakeConfig(/*maxIterations=*/2);
    auto engine = MakeEngine(config);

    std::vector<std::string> events;
    engine.SetEventSink([&events](const std::string& line) { events.push_back(line); });

    cppcoder::ResearchResult result;
    ASSERT_NO_THROW(result = engine.Research(MatchingQuestion()));
    ExpectWellFormedResult(result, config);

    // Research() always emits at least a "question" event up front and a
    // "complete" event at the end, regardless of outcome, so this must be
    // non-empty for any completed run -- we don't assert exact content or
    // count, just that events actually fired.
    EXPECT_FALSE(events.empty());
}

TEST_F(ResearchEngineLiveTest, VeryRestrictiveMaxInitialKeywordsStillReturnsWellFormedResult) {
    cppcoder_test::SkipUnlessOllamaReady();

    auto config = MakeConfig(/*maxIterations=*/1, /*maxInitialKeywords=*/1);
    auto engine = MakeEngine(config);

    cppcoder::ResearchResult result;
    ASSERT_NO_THROW(result = engine.Research(MatchingQuestion()));
    ExpectWellFormedResult(result, config);
}

TEST_F(ResearchEngineLiveTest, WallClockIsNonNegativeAcrossDifferentIterationBudgets) {
    cppcoder_test::SkipUnlessOllamaReady();

    auto configSmall = MakeConfig(/*maxIterations=*/1);
    auto engineSmall = MakeEngine(configSmall);
    cppcoder::ResearchResult resultSmall;
    ASSERT_NO_THROW(resultSmall = engineSmall.Research(MatchingQuestion()));
    ExpectWellFormedResult(resultSmall, configSmall);

    auto configLarger = MakeConfig(/*maxIterations=*/3);
    auto engineLarger = MakeEngine(configLarger);
    cppcoder::ResearchResult resultLarger;
    ASSERT_NO_THROW(resultLarger = engineLarger.Research(MatchingQuestion()));
    ExpectWellFormedResult(resultLarger, configLarger);

    // Deliberately not comparing resultSmall vs resultLarger against each
    // other: with a small local model, per-call latency variance can
    // easily swamp any "more iterations should take longer" signal.
    EXPECT_GE(resultSmall.wallClock.count(), 0);
    EXPECT_GE(resultLarger.wallClock.count(), 0);
}

TEST_F(ResearchEngineLiveTest, TwoIndependentEnginesBothProduceWellFormedResults) {
    cppcoder_test::SkipUnlessOllamaReady();

    auto configOne = MakeConfig(/*maxIterations=*/1);
    auto engineOne = MakeEngine(configOne);

    auto configTwo = MakeConfig(/*maxIterations=*/2);
    auto engineTwo = MakeEngine(configTwo);

    cppcoder::ResearchResult resultOne;
    ASSERT_NO_THROW(resultOne = engineOne.Research(MatchingQuestion()));
    ExpectWellFormedResult(resultOne, configOne);

    cppcoder::ResearchResult resultTwo;
    ASSERT_NO_THROW(resultTwo = engineTwo.Research(NonMatchingQuestion()));
    ExpectWellFormedResult(resultTwo, configTwo);
}

TEST_F(ResearchEngineLiveTest, TerminationReasonIsAlwaysNonEmpty) {
    cppcoder_test::SkipUnlessOllamaReady();

    auto matchConfig = MakeConfig(/*maxIterations=*/1);
    auto matchEngine = MakeEngine(matchConfig);
    cppcoder::ResearchResult matchResult;
    ASSERT_NO_THROW(matchResult = matchEngine.Research(MatchingQuestion()));
    EXPECT_FALSE(matchResult.terminationReason.empty());

    auto noMatchConfig = MakeConfig(/*maxIterations=*/1);
    auto noMatchEngine = MakeEngine(noMatchConfig);
    cppcoder::ResearchResult noMatchResult;
    ASSERT_NO_THROW(noMatchResult = noMatchEngine.Research(NonMatchingQuestion()));
    EXPECT_FALSE(noMatchResult.terminationReason.empty());
}

TEST_F(ResearchEngineLiveTest, QuestionWithSpecialCharactersDoesNotCrash) {
    cppcoder_test::SkipUnlessOllamaReady();

    auto config = MakeConfig(/*maxIterations=*/1);
    auto engine = MakeEngine(config);

    const std::string question =
        "What does DeriveKey() do?! (see crypto.cpp) -- explain @#$%^&*()_+ \"quoted\" & <tags>";

    cppcoder::ResearchResult result;
    ASSERT_NO_THROW(result = engine.Research(question));
    ExpectWellFormedResult(result, config);
}

TEST_F(ResearchEngineLiveTest, VerySmallTokenBudgetCompletesWithoutCrashing) {
    cppcoder_test::SkipUnlessOllamaReady();

    auto config = MakeConfig(/*maxIterations=*/1, /*maxInitialKeywords=*/5,
                              /*tokenBudgetPerTask=*/500);
    auto engine = MakeEngine(config);

    cppcoder::ResearchResult result;
    ASSERT_NO_THROW(result = engine.Research(MatchingQuestion()));
    ExpectWellFormedResult(result, config);
}

TEST_F(ResearchEngineLiveTest, AnsweredTrueImpliesNonEmptyAnswerAndFindings) {
    cppcoder_test::SkipUnlessOllamaReady();

    // A slightly larger (still capped) iteration budget to give the
    // worker/judge loop a realistic chance at a successful finding; the
    // assertion below only fires conditionally, so it's still safe if
    // this run doesn't happen to answer.
    auto config = MakeConfig(/*maxIterations=*/3);
    auto engine = MakeEngine(config);

    cppcoder::ResearchResult result;
    ASSERT_NO_THROW(result = engine.Research(MatchingQuestion()));
    ExpectWellFormedResult(result, config);

    if (result.answered) {
        EXPECT_FALSE(result.answer.empty());
        EXPECT_FALSE(result.successfulFindings.empty());
    }
}

TEST_F(ResearchEngineLiveTest, SeedInitialTasksIntegrationWithKnownMatchingFile) {
    cppcoder_test::SkipUnlessOllamaReady();

    // ParseDocument is the only identifier in parser.cpp, so a question
    // built around it exercises the same seed-matching path already
    // covered offline in ResearchEngineTests.cpp, but now through the
    // full Research() loop against a real model.
    auto config = MakeConfig(/*maxIterations=*/1);
    auto engine = MakeEngine(config);

    cppcoder::ResearchResult result;
    ASSERT_NO_THROW(result = engine.Research("What does ParseDocument do in parser.cpp?"));
    ExpectWellFormedResult(result, config);
}

TEST_F(ResearchEngineLiveTest, MaxIterationsZeroReturnsWellFormedResult) {
    cppcoder_test::SkipUnlessOllamaReady();

    // Per src/ResearchEngine.cpp, the while-loop's first check is
    // `iterations >= config_.maxIterations`, evaluated as 0 >= 0 before
    // any worker/judge call happens -- so with maxIterations=0 the loop
    // (if reached at all) breaks immediately with iterationsRun staying
    // at 0. It is not clamped up to 1. ExpectWellFormedResult's
    // `iterationsRun <= config.maxIterations` check combined with
    // `iterationsRun >= 0` already pins this down to exactly 0 for this
    // config, without needing to special-case which return path fired.
    auto config = MakeConfig(/*maxIterations=*/0);
    auto engine = MakeEngine(config);

    cppcoder::ResearchResult result;
    ASSERT_NO_THROW(result = engine.Research(MatchingQuestion()));
    ExpectWellFormedResult(result, config);
    EXPECT_EQ(result.iterationsRun, 0);
}

TEST_F(ResearchEngineLiveTest, FreshEngineAfterPriorRunProducesIndependentWellFormedResult) {
    cppcoder_test::SkipUnlessOllamaReady();

    auto configFirst = MakeConfig(/*maxIterations=*/1);
    auto engineFirst = MakeEngine(configFirst);
    cppcoder::ResearchResult resultFirst;
    ASSERT_NO_THROW(resultFirst = engineFirst.Research(MatchingQuestion()));
    ExpectWellFormedResult(resultFirst, configFirst);

    // Brand new ResearchEngine (fresh OllamaClient + CodebaseScanner +
    // config), same underlying temp codebase. Confirms there's no
    // cross-run/cross-instance state leak (e.g. a queue or visited-set
    // that outlives a single Research() call).
    auto configSecond = MakeConfig(/*maxIterations=*/1);
    auto engineSecond = MakeEngine(configSecond);
    cppcoder::ResearchResult resultSecond;
    ASSERT_NO_THROW(resultSecond = engineSecond.Research(MatchingQuestion()));
    ExpectWellFormedResult(resultSecond, configSecond);
}
