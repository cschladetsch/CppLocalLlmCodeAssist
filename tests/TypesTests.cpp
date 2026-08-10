#include "cppcoder/Types.h"

#include <gtest/gtest.h>

#include <type_traits>

using cppcoder::EstimateTokens;
using cppcoder::Task;
using cppcoder::WorkerOutcome;

TEST(EstimateTokensTest, EmptyStringIsNonZero) {
    // The +1 fudge factor means even empty input estimates to 1 token.
    EXPECT_EQ(EstimateTokens(""), 1u);
}

TEST(EstimateTokensTest, ScalesRoughlyWithLength) {
    std::string text(400, 'x');
    EXPECT_EQ(EstimateTokens(text), 101u);  // 400/4 + 1
}

TEST(EstimateTokensTest, ShortStringUnderOneCharPerFour) {
    EXPECT_EQ(EstimateTokens("abc"), 1u);  // 3/4 + 1 == 0 + 1
}

TEST(EstimateTokensTest, MonotonicWithLength) {
    EXPECT_LT(EstimateTokens("short"), EstimateTokens(std::string(1000, 'a')));
}

TEST(EstimateTokensTest, LargeBudgetTaskUnderTypicalContextWindow) {
    // Sanity check against the spec's empirical 100-150K token ceiling.
    std::string content(600'000, 'a');  // ~150K tokens
    EXPECT_LE(EstimateTokens(content), 150'001u);
}

TEST(TaskDefaultsTest, AggregateDefaultsAreSane) {
    Task t;
    EXPECT_TRUE(t.id.empty());
    EXPECT_TRUE(t.targetArea.empty());
    EXPECT_EQ(t.depth, 0);
    EXPECT_FALSE(t.repeatable);
    EXPECT_TRUE(t.repeatTargets.empty());
    EXPECT_TRUE(t.parentId.empty());
}

TEST(TaskDefaultsTest, AggregateInitPopulatesGivenFields) {
    Task t{"id1", "area/path", "find X", "X is found"};
    EXPECT_EQ(t.id, "id1");
    EXPECT_EQ(t.targetArea, "area/path");
    EXPECT_EQ(t.researchGoal, "find X");
    EXPECT_EQ(t.successCriteria, "X is found");
    EXPECT_EQ(t.depth, 0);
}

TEST(WorkerOutcomeTest, ValuesAreDistinct) {
    EXPECT_NE(WorkerOutcome::Success, WorkerOutcome::NoInformation);
    EXPECT_NE(WorkerOutcome::Success, WorkerOutcome::PartialWithDirections);
    EXPECT_NE(WorkerOutcome::PartialWithDirections, WorkerOutcome::NoInformation);
}

TEST(EstimateTokensTest, ExactMultipleOfFourBoundary) {
    // 4/4 + 1 == 2: the boundary right at a whole "token" of input.
    EXPECT_EQ(EstimateTokens("abcd"), 2u);
    EXPECT_EQ(EstimateTokens(std::string(8, 'x')), 3u);  // 8/4 + 1 == 3
}

TEST(EstimateTokensTest, NonMultipleOfFourLengthsFloorDivision) {
    // Integer division truncates toward zero, so lengths within the same
    // "band" of four all collapse to the same estimate.
    EXPECT_EQ(EstimateTokens(std::string(5, 'a')), 2u);  // 5/4 + 1 == 2
    EXPECT_EQ(EstimateTokens(std::string(6, 'a')), 2u);  // 6/4 + 1 == 2
    EXPECT_EQ(EstimateTokens(std::string(7, 'a')), 2u);  // 7/4 + 1 == 2
    EXPECT_EQ(EstimateTokens(std::string(9, 'a')), 3u);  // 9/4 + 1 == 3
}

TEST(EstimateTokensTest, VeryLongStringHandledWithoutOverflow) {
    std::string content(100'000, 'z');
    EXPECT_EQ(EstimateTokens(content), 25'001u);
}

TEST(EstimateTokensTest, MultiByteUtf8CountsBytesNotCodepoints) {
    // "日本語" is 3 codepoints but each encodes to 3 bytes in UTF-8, so the
    // std::string is 9 bytes long. EstimateTokens operates on byte length
    // (via std::string_view::size()), not codepoint count.
    std::string text = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";  // "日本語"
    ASSERT_EQ(text.size(), 9u);
    EXPECT_EQ(EstimateTokens(text), 3u);  // 9/4 + 1 == 3

    // Had it (incorrectly) counted the 3 codepoints instead of 9 bytes,
    // the estimate would be 3/4 + 1 == 1, which is what we rule out here.
    EXPECT_NE(EstimateTokens(text), 1u);
}

TEST(EstimateTokensTest, StringViewFromTemporarySubstring) {
    std::string source = "0123456789abcdef";
    // std::string::substr returns a temporary std::string; it must convert
    // implicitly to std::string_view for the duration of this call.
    EXPECT_EQ(EstimateTokens(source.substr(4, 8)), 3u);  // 8/4 + 1 == 3

    std::string temp = source.substr(0, 7);
    EXPECT_EQ(EstimateTokens(std::string_view(temp)), 2u);  // 7/4 + 1 == 2
}

TEST(TaskTest, RepeatTargetsDeepCopiedOnCopyConstruction) {
    Task original;
    original.repeatable = true;
    original.repeatTargets = {"areaA", "areaB", "areaC"};

    Task copy = original;
    ASSERT_EQ(copy.repeatTargets.size(), 3u);
    EXPECT_EQ(copy.repeatTargets[0], "areaA");
    EXPECT_EQ(copy.repeatTargets[2], "areaC");
    EXPECT_TRUE(copy.repeatable);

    // Mutating the original's vector must not affect the copy: Task holds
    // its own std::vector, not a shared reference.
    original.repeatTargets.push_back("areaD");
    EXPECT_EQ(copy.repeatTargets.size(), 3u);
    EXPECT_EQ(original.repeatTargets.size(), 4u);
}

TEST(TaskDefaultsTest, TwoDefaultConstructedTasksAreFieldwiseEqual) {
    // Task has no operator==, so equality is checked field-by-field.
    Task a;
    Task b;
    EXPECT_EQ(a.id, b.id);
    EXPECT_EQ(a.targetArea, b.targetArea);
    EXPECT_EQ(a.researchGoal, b.researchGoal);
    EXPECT_EQ(a.successCriteria, b.successCriteria);
    EXPECT_EQ(a.depth, b.depth);
    EXPECT_EQ(a.repeatable, b.repeatable);
    EXPECT_EQ(a.repeatTargets, b.repeatTargets);
    EXPECT_EQ(a.parentId, b.parentId);
}

TEST(FindingAndEditFindingTest, SuggestedDirectionsAndDurationHoldNonTrivialValues) {
    cppcoder::Finding finding;
    finding.outcome = WorkerOutcome::PartialWithDirections;
    finding.suggestedDirections.push_back(Task{"t1", "area1", "goal1", "criteria1"});
    finding.suggestedDirections.push_back(Task{"t2", "area2", "goal2", "criteria2"});
    finding.duration = std::chrono::milliseconds(1500);
    finding.promptTokensApprox = 42;

    ASSERT_EQ(finding.suggestedDirections.size(), 2u);
    EXPECT_EQ(finding.suggestedDirections[1].id, "t2");
    EXPECT_EQ(finding.duration.count(), 1500);
    EXPECT_EQ(finding.promptTokensApprox, 42u);

    cppcoder::EditFinding editFinding;
    editFinding.outcome = cppcoder::EditOutcome::PartialWithDirections;
    editFinding.suggestedDirections.push_back(Task{"t3", "area3", "goal3", "criteria3"});
    editFinding.duration = std::chrono::milliseconds(2500);

    ASSERT_EQ(editFinding.suggestedDirections.size(), 1u);
    EXPECT_EQ(editFinding.suggestedDirections[0].id, "t3");
    EXPECT_EQ(editFinding.duration.count(), 2500);
}

TEST(EventSinkTest, InvokedLambdaCapturesAppendedState) {
    std::vector<std::string> events;
    cppcoder::EventSink sink = [&events](const std::string& event) {
        events.push_back(event);
    };

    sink(R"({"event":"task_queued"})");
    sink(R"({"event":"task_done"})");

    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0], R"({"event":"task_queued"})");
    EXPECT_EQ(events[1], R"({"event":"task_done"})");
}

TEST(ProposedEditAndOutcomeEnumsTest, DefaultEditFieldsEmptyAndOutcomeDefaultsDiffer) {
    cppcoder::ProposedEdit edit;
    EXPECT_TRUE(edit.path.empty());
    EXPECT_TRUE(edit.newContent.empty());
    EXPECT_TRUE(edit.description.empty());

    // WorkerOutcome and EditOutcome share member names (Success,
    // PartialWithDirections) but are distinct types with distinct defaults
    // on their respective owning structs.
    static_assert(!std::is_same_v<WorkerOutcome, cppcoder::EditOutcome>,
                  "WorkerOutcome and EditOutcome must be distinct types");

    cppcoder::Finding finding;
    cppcoder::EditFinding editFinding;
    EXPECT_EQ(finding.outcome, WorkerOutcome::NoInformation);
    EXPECT_EQ(editFinding.outcome, cppcoder::EditOutcome::NoChangeNeeded);

    // The shared member names line up on the same underlying ordinal even
    // though they belong to different enum types.
    EXPECT_EQ(static_cast<int>(WorkerOutcome::Success), static_cast<int>(cppcoder::EditOutcome::Success));
    EXPECT_EQ(static_cast<int>(WorkerOutcome::PartialWithDirections),
              static_cast<int>(cppcoder::EditOutcome::PartialWithDirections));
}
