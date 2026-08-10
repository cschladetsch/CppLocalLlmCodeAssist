#include "cppcoder/Worker.h"

#include <gtest/gtest.h>

using cppcoder::Finding;
using cppcoder::Worker;
using cppcoder::WorkerOutcome;

TEST(ParseWorkerResponseTest, SuccessOutcomeParsed) {
    std::string raw = R"({"outcome":"success","summary":"found it","directions":[]})";
    Finding f = Worker::ParseWorkerResponse(raw, "src/foo.cpp");
    EXPECT_EQ(f.outcome, WorkerOutcome::Success);
    EXPECT_EQ(f.summary, "found it");
    EXPECT_TRUE(f.suggestedDirections.empty());
    EXPECT_EQ(f.areaInvestigated, "src/foo.cpp");
}

TEST(ParseWorkerResponseTest, PartialOutcomeWithDirectionsParsed) {
    std::string raw = R"({
        "outcome":"partial",
        "summary":"some info",
        "directions":[
            {"target_area":"src/bar.cpp","research_goal":"find bar","success_criteria":"bar found"}
        ]
    })";
    Finding f = Worker::ParseWorkerResponse(raw, "src/foo.cpp");
    EXPECT_EQ(f.outcome, WorkerOutcome::PartialWithDirections);
    ASSERT_EQ(f.suggestedDirections.size(), 1u);
    EXPECT_EQ(f.suggestedDirections[0].targetArea, "src/bar.cpp");
    EXPECT_EQ(f.suggestedDirections[0].researchGoal, "find bar");
    EXPECT_EQ(f.suggestedDirections[0].successCriteria, "bar found");
}

TEST(ParseWorkerResponseTest, NoInformationOutcomeParsed) {
    std::string raw = R"({"outcome":"no_information","summary":"","directions":[]})";
    Finding f = Worker::ParseWorkerResponse(raw, "src/foo.cpp");
    EXPECT_EQ(f.outcome, WorkerOutcome::NoInformation);
}

TEST(ParseWorkerResponseTest, UnknownOutcomeStringDefaultsToNoInformation) {
    std::string raw = R"({"outcome":"maybe","summary":""})";
    Finding f = Worker::ParseWorkerResponse(raw, "src/foo.cpp");
    EXPECT_EQ(f.outcome, WorkerOutcome::NoInformation);
}

TEST(ParseWorkerResponseTest, ResponseWrappedInProseAndMarkdownFences) {
    std::string raw = "Sure, here's my analysis:\n```json\n"
                       R"({"outcome":"success","summary":"ok","directions":[]})"
                       "\n```\nHope that helps!";
    Finding f = Worker::ParseWorkerResponse(raw, "src/foo.cpp");
    EXPECT_EQ(f.outcome, WorkerOutcome::Success);
    EXPECT_EQ(f.summary, "ok");
}

TEST(ParseWorkerResponseTest, MissingDirectionsFieldDefaultsToEmpty) {
    std::string raw = R"({"outcome":"partial","summary":"x"})";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_TRUE(f.suggestedDirections.empty());
}

TEST(ParseWorkerResponseTest, DirectionsMissingTargetAreaAreFiltered) {
    std::string raw = R"({
        "outcome":"partial",
        "directions":[
            {"research_goal":"no area given"},
            {"target_area":"src/valid.cpp","research_goal":"valid one"}
        ]
    })";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    ASSERT_EQ(f.suggestedDirections.size(), 1u);
    EXPECT_EQ(f.suggestedDirections[0].targetArea, "src/valid.cpp");
}

TEST(ParseWorkerResponseTest, DirectionsMissingResearchGoalAreFiltered) {
    std::string raw = R"({
        "outcome":"partial",
        "directions":[
            {"target_area":"src/no_goal.cpp"}
        ]
    })";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_TRUE(f.suggestedDirections.empty());
}

TEST(ParseWorkerResponseTest, MalformedJsonFallsBackToNoInformation) {
    std::string raw = R"({"outcome": "success", "summary": )";  // truncated/invalid
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::NoInformation);
}

TEST(ParseWorkerResponseTest, EmptyRawStringFallsBackToNoInformation) {
    Finding f = Worker::ParseWorkerResponse("", "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::NoInformation);
}

TEST(ParseWorkerResponseTest, NoJsonAtAllFallsBackToNoInformation) {
    Finding f = Worker::ParseWorkerResponse("I couldn't find any JSON to give you.", "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::NoInformation);
}

TEST(ParseWorkerResponseTest, MultipleDirectionsAllParsedWhenValid) {
    std::string raw = R"({
        "outcome":"partial",
        "directions":[
            {"target_area":"a.cpp","research_goal":"g1","success_criteria":"c1"},
            {"target_area":"b.cpp","research_goal":"g2","success_criteria":"c2"},
            {"target_area":"c.cpp","research_goal":"g3","success_criteria":"c3"}
        ]
    })";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.suggestedDirections.size(), 3u);
}

TEST(ParseWorkerResponseTest, GeneratedDirectionIdsAreNonEmptyAndUnique) {
    std::string raw = R"({
        "outcome":"partial",
        "directions":[
            {"target_area":"a.cpp","research_goal":"g1"},
            {"target_area":"b.cpp","research_goal":"g2"}
        ]
    })";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    ASSERT_EQ(f.suggestedDirections.size(), 2u);
    EXPECT_FALSE(f.suggestedDirections[0].id.empty());
    EXPECT_FALSE(f.suggestedDirections[1].id.empty());
    EXPECT_NE(f.suggestedDirections[0].id, f.suggestedDirections[1].id);
}

TEST(ParseWorkerResponseTest, ExplicitEmptyDirectionsArrayParsed) {
    std::string raw = R"({"outcome":"partial","summary":"nothing to suggest","directions":[]})";
    Finding f = Worker::ParseWorkerResponse(raw, "src/empty.cpp");
    EXPECT_EQ(f.outcome, WorkerOutcome::PartialWithDirections);
    EXPECT_EQ(f.summary, "nothing to suggest");
    EXPECT_TRUE(f.suggestedDirections.empty());
    EXPECT_EQ(f.areaInvestigated, "src/empty.cpp");
}

TEST(ParseWorkerResponseTest, ExtraUnexpectedFieldsInDirectionsAreIgnored) {
    std::string raw = R"({
        "outcome":"partial",
        "summary":"has extras",
        "directions":[
            {"target_area":"src/extra.cpp","research_goal":"look here","success_criteria":"found",
             "confidence":0.9,"notes":"unexpected field","priority":1}
        ]
    })";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    ASSERT_EQ(f.suggestedDirections.size(), 1u);
    EXPECT_EQ(f.suggestedDirections[0].targetArea, "src/extra.cpp");
    EXPECT_EQ(f.suggestedDirections[0].researchGoal, "look here");
    EXPECT_EQ(f.suggestedDirections[0].successCriteria, "found");
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseWorkerResponseTest, MixOfValidAndInvalidDirectionsKeepsOnlyValid) {
    std::string raw = R"({
        "outcome":"partial",
        "directions":[
            {"research_goal":"missing area entirely"},
            {"target_area":"src/no_goal.cpp"},
            {"target_area":"src/good.cpp","research_goal":"the good one","success_criteria":"met"}
        ]
    })";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    ASSERT_EQ(f.suggestedDirections.size(), 1u);
    EXPECT_EQ(f.suggestedDirections[0].targetArea, "src/good.cpp");
    EXPECT_EQ(f.suggestedDirections[0].researchGoal, "the good one");
    EXPECT_EQ(f.suggestedDirections[0].successCriteria, "met");
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseWorkerResponseTest, SummaryWithEmbeddedNewlinesAndTabsPreserved) {
    std::string raw = R"({"outcome":"success","summary":"line one\nline two\tindented","directions":[]})";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::Success);
    EXPECT_EQ(f.summary, "line one\nline two\tindented");
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseWorkerResponseTest, SummaryWithEmbeddedUnicodeAndEmojiPreserved) {
    const std::string summaryText = "café found \xF0\x9F\x98\x80 nice";
    std::string raw = R"({"outcome":"success","summary":")" + summaryText + R"(","directions":[]})";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::Success);
    EXPECT_EQ(f.summary, summaryText);
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseWorkerResponseTest, PrettyPrintedJsonWithExtraWhitespaceParsedCorrectly) {
    std::string raw = "{\n"
                       "    \"outcome\"    :    \"success\"   ,\n\n\n"
                       "    \"summary\" :    \"spaced out\"    ,\n"
                       "    \"directions\"   :   [   ]\n"
                       "}";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::Success);
    EXPECT_EQ(f.summary, "spaced out");
    EXPECT_TRUE(f.suggestedDirections.empty());
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseWorkerResponseTest, ProseBeforeAndAfterJsonBlockBothParsedCorrectly) {
    std::string raw = "Let me think about this for a moment.\n"
                       R"({"outcome":"partial","summary":"leads found","directions":[]})"
                       "\nI hope this analysis is useful to you, let me know if you need more.";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::PartialWithDirections);
    EXPECT_EQ(f.summary, "leads found");
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseWorkerResponseTest, MarkdownFenceWithoutLanguageTagParsed) {
    std::string raw = "Here is the result:\n```\n"
                       R"({"outcome":"success","summary":"no lang tag","directions":[]})"
                       "\n```";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::Success);
    EXPECT_EQ(f.summary, "no lang tag");
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseWorkerResponseTest, OutcomeFieldMissingEntirelyDefaultsToNoInformation) {
    std::string raw = R"({"summary":"no outcome key present here"})";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::NoInformation);
    EXPECT_EQ(f.summary, "no outcome key present here");
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseWorkerResponseTest, NumericOutcomeValueFallsBackToNoInformationGracefully) {
    std::string raw = R"({"outcome": 1, "summary": "numeric outcome", "directions": []})";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::NoInformation);
    EXPECT_TRUE(f.suggestedDirections.empty());
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseWorkerResponseTest, BooleanOutcomeValueFallsBackToNoInformationGracefully) {
    std::string raw = R"({"outcome": true, "summary": "boolean outcome"})";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::NoInformation);
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseWorkerResponseTest, OutcomeStringCaseMismatchFallsBackToNoInformation) {
    std::string raw = R"({"outcome":"Success","summary":"wrong case"})";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::NoInformation);
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseWorkerResponseTest, DeeplyTruncatedJsonMissingClosingBraceFallsBackToNoInformation) {
    std::string raw = R"({"outcome":"success","summary":"cut off mid-stream)";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::NoInformation);
    EXPECT_TRUE(f.suggestedDirections.empty());
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseWorkerResponseTest, UnbalancedNestedBracketsMalformedJsonFallsBackToNoInformation) {
    std::string raw = R"({"outcome":"success", "summary": "test", "directions": [ {"target_area": "a" })";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::NoInformation);
    EXPECT_TRUE(f.suggestedDirections.empty());
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseWorkerResponseTest, MissingSummaryFieldDefaultsToEmptyString) {
    std::string raw = R"({"outcome":"success","directions":[]})";
    Finding f = Worker::ParseWorkerResponse(raw, "area");
    EXPECT_EQ(f.outcome, WorkerOutcome::Success);
    EXPECT_EQ(f.summary, "");
    EXPECT_EQ(f.areaInvestigated, "area");
}
