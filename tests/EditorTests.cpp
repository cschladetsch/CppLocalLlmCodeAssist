#include "cppcoder/Editor.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

using cppcoder::EditFinding;
using cppcoder::EditOutcome;
using cppcoder::Editor;

TEST(ParseEditResponseTest, SuccessOutcomeWithEditParsed) {
    std::string raw = R"({
        "outcome":"success",
        "summary":"renamed the function",
        "edits":[
            {"path":"src/foo.cpp","new_content":"void Bar() {}\n","description":"renamed Foo to Bar"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "src/foo.cpp");
    EXPECT_EQ(f.outcome, EditOutcome::Success);
    EXPECT_EQ(f.summary, "renamed the function");
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_EQ(f.edits[0].path, "src/foo.cpp");
    EXPECT_EQ(f.edits[0].newContent, "void Bar() {}\n");
    EXPECT_EQ(f.edits[0].description, "renamed Foo to Bar");
    EXPECT_TRUE(f.suggestedDirections.empty());
    EXPECT_EQ(f.areaInvestigated, "src/foo.cpp");
}

TEST(ParseEditResponseTest, NoChangeOutcomeParsed) {
    std::string raw = R"({"outcome":"no_change","summary":"","edits":[],"directions":[]})";
    EditFinding f = Editor::ParseEditResponse(raw, "src/foo.cpp");
    EXPECT_EQ(f.outcome, EditOutcome::NoChangeNeeded);
    EXPECT_TRUE(f.edits.empty());
}

TEST(ParseEditResponseTest, PartialOutcomeWithEditsAndDirectionsParsed) {
    std::string raw = R"({
        "outcome":"partial",
        "summary":"updated caller",
        "edits":[
            {"path":"src/foo.cpp","new_content":"Bar();\n","description":"updated call site"}
        ],
        "directions":[
            {"target_area":"src/bar.cpp","research_goal":"rename Foo to Bar here too","success_criteria":"no more Foo references"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "src/foo.cpp");
    EXPECT_EQ(f.outcome, EditOutcome::PartialWithDirections);
    ASSERT_EQ(f.edits.size(), 1u);
    ASSERT_EQ(f.suggestedDirections.size(), 1u);
    EXPECT_EQ(f.suggestedDirections[0].targetArea, "src/bar.cpp");
    EXPECT_EQ(f.suggestedDirections[0].researchGoal, "rename Foo to Bar here too");
    EXPECT_EQ(f.suggestedDirections[0].successCriteria, "no more Foo references");
}

TEST(ParseEditResponseTest, UnknownOutcomeStringDefaultsToNoChangeNeeded) {
    std::string raw = R"({"outcome":"maybe","summary":""})";
    EditFinding f = Editor::ParseEditResponse(raw, "src/foo.cpp");
    EXPECT_EQ(f.outcome, EditOutcome::NoChangeNeeded);
}

TEST(ParseEditResponseTest, ResponseWrappedInProseAndMarkdownFences) {
    std::string raw = "Sure, here's the change:\n```json\n"
                       R"({"outcome":"success","summary":"ok","edits":[{"path":"a.cpp","new_content":"x"}]})"
                       "\n```\nHope that helps!";
    EditFinding f = Editor::ParseEditResponse(raw, "src/foo.cpp");
    EXPECT_EQ(f.outcome, EditOutcome::Success);
    EXPECT_EQ(f.summary, "ok");
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_EQ(f.edits[0].path, "a.cpp");
}

TEST(ParseEditResponseTest, MissingEditsFieldDefaultsToEmpty) {
    std::string raw = R"({"outcome":"success","summary":"x"})";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    EXPECT_TRUE(f.edits.empty());
}

TEST(ParseEditResponseTest, EditsMissingPathAreFiltered) {
    std::string raw = R"({
        "outcome":"success",
        "edits":[
            {"new_content":"no path given"},
            {"path":"src/valid.cpp","new_content":"valid one"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_EQ(f.edits[0].path, "src/valid.cpp");
}

TEST(ParseEditResponseTest, DirectionsMissingTargetAreaAreFiltered) {
    std::string raw = R"({
        "outcome":"partial",
        "directions":[
            {"research_goal":"no area given"},
            {"target_area":"src/valid.cpp","research_goal":"valid one"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    ASSERT_EQ(f.suggestedDirections.size(), 1u);
    EXPECT_EQ(f.suggestedDirections[0].targetArea, "src/valid.cpp");
}

TEST(ParseEditResponseTest, MalformedJsonFallsBackToNoChangeNeeded) {
    std::string raw = R"({"outcome": "success", "edits": )";  // truncated/invalid
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    EXPECT_EQ(f.outcome, EditOutcome::NoChangeNeeded);
}

TEST(ParseEditResponseTest, EmptyRawStringFallsBackToNoChangeNeeded) {
    EditFinding f = Editor::ParseEditResponse("", "area");
    EXPECT_EQ(f.outcome, EditOutcome::NoChangeNeeded);
}

TEST(ParseEditResponseTest, NoJsonAtAllFallsBackToNoChangeNeeded) {
    EditFinding f = Editor::ParseEditResponse("I don't think anything needs to change.", "area");
    EXPECT_EQ(f.outcome, EditOutcome::NoChangeNeeded);
}

TEST(ParseEditResponseTest, MultipleEditsAllParsedWhenValid) {
    std::string raw = R"({
        "outcome":"success",
        "edits":[
            {"path":"a.cpp","new_content":"a"},
            {"path":"b.cpp","new_content":"b"},
            {"path":"c.cpp","new_content":"c"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    EXPECT_EQ(f.edits.size(), 3u);
}

TEST(ParseEditResponseTest, StripsEchoedScannerHeaderMarkerFromNewContent) {
    // CodebaseScanner prepends "// ==== <path> ====" before each file's
    // content in the prompt; small local models sometimes echo it back
    // as if it were real file content despite being told not to.
    std::string raw = R"({
        "outcome":"success",
        "edits":[
            {"path":"foo.cpp","new_content":"\n// ==== foo.cpp ====\nint Bar() { return 1; }\n"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "foo.cpp");
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_EQ(f.edits[0].newContent, "int Bar() { return 1; }\n");
}

TEST(ParseEditResponseTest, ContentWithoutEchoedMarkerIsUnchanged) {
    std::string raw = R"({
        "outcome":"success",
        "edits":[
            {"path":"foo.cpp","new_content":"int Bar() { return 1; }\n"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "foo.cpp");
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_EQ(f.edits[0].newContent, "int Bar() { return 1; }\n");
}

TEST(ParseEditResponseTest, GeneratedDirectionIdsAreNonEmptyAndUnique) {
    std::string raw = R"({
        "outcome":"partial",
        "directions":[
            {"target_area":"a.cpp","research_goal":"g1"},
            {"target_area":"b.cpp","research_goal":"g2"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    ASSERT_EQ(f.suggestedDirections.size(), 2u);
    EXPECT_FALSE(f.suggestedDirections[0].id.empty());
    EXPECT_FALSE(f.suggestedDirections[1].id.empty());
    EXPECT_NE(f.suggestedDirections[0].id, f.suggestedDirections[1].id);
}

TEST(ParseEditResponseTest, EditMissingNewContentDefaultsToEmptyString) {
    std::string raw = R"({
        "outcome":"success",
        "edits":[
            {"path":"src/foo.cpp","description":"no content given"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_EQ(f.edits[0].path, "src/foo.cpp");
    EXPECT_EQ(f.edits[0].newContent, "");
    EXPECT_EQ(f.edits[0].description, "no content given");
}

TEST(ParseEditResponseTest, EditWithEmptyStringPathIsFiltered) {
    std::string raw = R"({
        "outcome":"success",
        "edits":[
            {"path":"","new_content":"orphaned content"},
            {"path":"ok.cpp","new_content":"kept content"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_EQ(f.edits[0].path, "ok.cpp");
}

TEST(ParseEditResponseTest, DescriptionFieldPreservedVerbatimIncludingUnicode) {
    // Both the JSON payload and the expected value are built from \x
    // hex-byte escapes spelling out the UTF-8 encoding of U+00E9 (e with
    // acute accent), U+2014 (em dash), and U+1F600 (grinning face emoji),
    // rather than literal multi-byte characters in the source -- \x
    // escapes insert exact byte values regardless of the compiler's
    // assumed source/execution charset, so this test can't be broken by
    // that setting.
    std::string raw =
        R"({"outcome":"success","edits":[{"path":"a.cpp","new_content":"x",)"
        R"("description":"Fixed caf)" "\xC3\xA9" R"( bug )" "\xE2\x80\x94" R"( )" "\xF0\x9F\x98\x80"
        R"( done"}]})";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_EQ(f.edits[0].description, "Fixed caf\xC3\xA9 bug \xE2\x80\x94 \xF0\x9F\x98\x80 done");
}

TEST(ParseEditResponseTest, EchoedMarkerForDifferentPathThanAreaIsStillStripped) {
    // StripScannerHeaderMarker only checks the "// ==== " prefix shape; it
    // does not compare the path inside the marker against the `area`
    // argument, so a marker naming an unrelated file is still stripped.
    std::string raw = R"({
        "outcome":"success",
        "edits":[
            {"path":"other.cpp","new_content":"\n// ==== unrelated/path.cpp ====\nint X() { return 2; }\n"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "other.cpp");
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_EQ(f.edits[0].newContent, "int X() { return 2; }\n");
}

TEST(ParseEditResponseTest, MarkerAppearingInMiddleOfContentIsNotStripped) {
    std::string raw = R"({
        "outcome":"success",
        "edits":[
            {"path":"foo.cpp","new_content":"int Foo() {}\n// ==== foo.cpp ====\nint Bar() {}\n"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "foo.cpp");
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_EQ(f.edits[0].newContent, "int Foo() {}\n// ==== foo.cpp ====\nint Bar() {}\n");
}

TEST(ParseEditResponseTest, MarkerStrippedOnlyOnEditThatHasIt) {
    std::string raw = R"({
        "outcome":"success",
        "edits":[
            {"path":"a.cpp","new_content":"\n// ==== a.cpp ====\nint A() {}\n"},
            {"path":"b.cpp","new_content":"int B() {}\n"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    ASSERT_EQ(f.edits.size(), 2u);
    EXPECT_EQ(f.edits[0].newContent, "int A() {}\n");
    EXPECT_EQ(f.edits[1].newContent, "int B() {}\n");
}

TEST(ParseEditResponseTest, MarkerWithoutTrailingNewlineIsNotStripped) {
    // StripScannerHeaderMarker looks for '\n' after the marker prefix to
    // find where the line ends; if there is none, it leaves content as-is
    // rather than stripping to end-of-string.
    std::string raw = R"({
        "outcome":"success",
        "edits":[
            {"path":"foo.cpp","new_content":"// ==== foo.cpp ===="}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "foo.cpp");
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_EQ(f.edits[0].newContent, "// ==== foo.cpp ====");
}

TEST(ParseEditResponseTest, ContentResemblingMarkerButMissingFullPrefixIsNotStripped) {
    // Only three '=' characters before the letter, so it does not match
    // the exact 8-character "// ==== " prefix and is left untouched.
    std::string raw = R"({
        "outcome":"success",
        "edits":[
            {"path":"foo.cpp","new_content":"// ===header===\ncode here\n"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "foo.cpp");
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_EQ(f.edits[0].newContent, "// ===header===\ncode here\n");
}

TEST(ParseEditResponseTest, MixOfValidAndMalformedEditEntriesFiltersInvalidOnes) {
    std::string raw = R"({
        "outcome":"success",
        "edits":[
            {},
            {"description":"only a description, no path or content"},
            {"path":"good.cpp","new_content":"good content","description":"ok"},
            {"path":"","new_content":"has content but empty path"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_EQ(f.edits[0].path, "good.cpp");
    EXPECT_EQ(f.edits[0].newContent, "good content");
}

TEST(ParseEditResponseTest, PartialOutcomeWithMultipleEditsAndMultipleDirectionsBothPopulated) {
    std::string raw = R"({
        "outcome":"partial",
        "summary":"two files changed, two more areas need work",
        "edits":[
            {"path":"a.cpp","new_content":"a"},
            {"path":"b.cpp","new_content":"b"}
        ],
        "directions":[
            {"target_area":"c.cpp","research_goal":"g1","success_criteria":"s1"},
            {"target_area":"d.cpp","research_goal":"g2","success_criteria":"s2"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    EXPECT_EQ(f.outcome, EditOutcome::PartialWithDirections);
    ASSERT_EQ(f.edits.size(), 2u);
    ASSERT_EQ(f.suggestedDirections.size(), 2u);
    EXPECT_EQ(f.suggestedDirections[0].targetArea, "c.cpp");
    EXPECT_EQ(f.suggestedDirections[1].targetArea, "d.cpp");
}

TEST(ParseEditResponseTest, PartialOutcomeWithEditsButEmptyDirectionsIsStillValid) {
    std::string raw = R"({
        "outcome":"partial",
        "edits":[
            {"path":"a.cpp","new_content":"a"}
        ],
        "directions":[]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    EXPECT_EQ(f.outcome, EditOutcome::PartialWithDirections);
    ASSERT_EQ(f.edits.size(), 1u);
    EXPECT_TRUE(f.suggestedDirections.empty());
}

TEST(ParseEditResponseTest, SummaryWithEmbeddedNewlinesAndMarkdownPreservedVerbatim) {
    std::string raw = R"({"outcome":"success","summary":"Line1\nLine2\n**bold** and `code`\n- item one\n- item two"})";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    EXPECT_EQ(f.summary, "Line1\nLine2\n**bold** and `code`\n- item one\n- item two");
}

TEST(ParseEditResponseTest, MultipleTopLevelJsonBlocksCausesParseFailureDefaultsToNoChange) {
    // ExtractJsonObject spans from the FIRST '{' to the LAST '}' in the raw
    // text. With two separate top-level objects, that span includes both
    // objects plus whatever sits between them, which is not valid JSON on
    // its own -- so this fails to parse rather than picking out "just the
    // first" object, and falls back to NoChangeNeeded.
    std::string raw = "First attempt: " +
                       std::string(R"({"outcome":"success","summary":"first"})") +
                       " on second thought: " +
                       std::string(R"({"outcome":"success","summary":"second"})");
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    EXPECT_EQ(f.outcome, EditOutcome::NoChangeNeeded);
    EXPECT_EQ(f.areaInvestigated, "area");
}

TEST(ParseEditResponseTest, LargeEditsArrayParsedInOrder) {
    std::ostringstream raw;
    raw << R"({"outcome":"success","edits":[)";
    constexpr int kCount = 12;
    for (int i = 0; i < kCount; ++i) {
        if (i > 0) raw << ",";
        raw << R"({"path":"file)" << i << R"(.cpp","new_content":"content )" << i << R"("})";
    }
    raw << "]}";

    EditFinding f = Editor::ParseEditResponse(raw.str(), "area");
    ASSERT_EQ(f.edits.size(), static_cast<std::size_t>(kCount));
    for (int i = 0; i < kCount; ++i) {
        EXPECT_EQ(f.edits[static_cast<std::size_t>(i)].path, "file" + std::to_string(i) + ".cpp");
        EXPECT_EQ(f.edits[static_cast<std::size_t>(i)].newContent, "content " + std::to_string(i));
    }
}

TEST(ParseEditResponseTest, PathsWithForwardAndBackwardSlashesPreservedVerbatim) {
    std::string raw = R"({
        "outcome":"success",
        "edits":[
            {"path":"src\\windows\\Foo.cpp","new_content":"a"},
            {"path":"src/unix/foo.cpp","new_content":"b"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    ASSERT_EQ(f.edits.size(), 2u);
    EXPECT_EQ(f.edits[0].path, "src\\windows\\Foo.cpp");
    EXPECT_EQ(f.edits[1].path, "src/unix/foo.cpp");
}

TEST(ParseEditResponseTest, DirectionsWithMissingResearchGoalFilteredButMissingSuccessCriteriaKept) {
    std::string raw = R"({
        "outcome":"partial",
        "directions":[
            {"target_area":"a.cpp","success_criteria":"sc only, no goal"},
            {"target_area":"b.cpp","research_goal":"goal present"}
        ]
    })";
    EditFinding f = Editor::ParseEditResponse(raw, "area");
    ASSERT_EQ(f.suggestedDirections.size(), 1u);
    EXPECT_EQ(f.suggestedDirections[0].targetArea, "b.cpp");
    EXPECT_EQ(f.suggestedDirections[0].researchGoal, "goal present");
    EXPECT_EQ(f.suggestedDirections[0].successCriteria, "");
}
