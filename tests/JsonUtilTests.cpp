#include "cppcoder/JsonUtil.h"

#include <gtest/gtest.h>

using cppcoder::ExtractJsonArray;
using cppcoder::ExtractJsonObject;

// ---------------- ExtractJsonObject ----------------

TEST(ExtractJsonObjectTest, PlainObject) {
    EXPECT_EQ(ExtractJsonObject(R"({"a":1})"), R"({"a":1})");
}

TEST(ExtractJsonObjectTest, ObjectWithLeadingProse) {
    EXPECT_EQ(ExtractJsonObject(R"(Sure, here you go: {"a":1})"), R"({"a":1})");
}

TEST(ExtractJsonObjectTest, ObjectWithTrailingProse) {
    EXPECT_EQ(ExtractJsonObject(R"({"a":1} hope that helps!)"), R"({"a":1})");
}

TEST(ExtractJsonObjectTest, ObjectWrappedInMarkdownFence) {
    EXPECT_EQ(ExtractJsonObject("```json\n{\"a\":1}\n```"), R"({"a":1})");
}

TEST(ExtractJsonObjectTest, NestedObjectKeepsOutermostBraces) {
    std::string nested = R"({"a":{"b":2}})";
    EXPECT_EQ(ExtractJsonObject(nested), nested);
}

TEST(ExtractJsonObjectTest, NoBracesReturnsEmpty) {
    EXPECT_EQ(ExtractJsonObject("no json here"), "");
}

TEST(ExtractJsonObjectTest, EmptyStringReturnsEmpty) {
    EXPECT_EQ(ExtractJsonObject(""), "");
}

TEST(ExtractJsonObjectTest, OnlyClosingBraceReturnsEmpty) {
    EXPECT_EQ(ExtractJsonObject("no open brace here }"), "");
}

TEST(ExtractJsonObjectTest, OnlyOpeningBraceReturnsEmpty) {
    EXPECT_EQ(ExtractJsonObject("{ no close brace"), "");
}

TEST(ExtractJsonObjectTest, MultipleObjectsPicksOutermostSpan) {
    // first '{' to last '}' -- this is a known, documented limitation
    // (not a real parser), so two sibling objects merge into one span.
    std::string input = R"({"a":1} and also {"b":2})";
    EXPECT_EQ(ExtractJsonObject(input), R"({"a":1} and also {"b":2})");
}

// ---------------- ExtractJsonArray ----------------

TEST(ExtractJsonArrayTest, PlainArray) {
    EXPECT_EQ(ExtractJsonArray(R"(["a","b"])"), R"(["a","b"])");
}

TEST(ExtractJsonArrayTest, ArrayWithSurroundingProse) {
    EXPECT_EQ(ExtractJsonArray(R"(Keywords: ["a","b"] -- done)"), R"(["a","b"])");
}

TEST(ExtractJsonArrayTest, EmptyArray) {
    EXPECT_EQ(ExtractJsonArray("[]"), "[]");
}

TEST(ExtractJsonArrayTest, NoBracketsReturnsEmpty) {
    EXPECT_EQ(ExtractJsonArray("nothing to see"), "");
}

TEST(ExtractJsonArrayTest, EmptyStringReturnsEmpty) {
    EXPECT_EQ(ExtractJsonArray(""), "");
}

TEST(ExtractJsonArrayTest, OnlyOpenBracketReturnsEmpty) {
    EXPECT_EQ(ExtractJsonArray("[ unterminated"), "");
}

// ---------------- Additional coverage ----------------

TEST(ExtractJsonArrayTest, MultipleArraysPicksOutermostSpan) {
    // first '[' to last ']' -- mirrors the object behavior: two sibling
    // arrays merge into a single (invalid) span.
    std::string input = R"(["a"] and also ["b"])";
    EXPECT_EQ(ExtractJsonArray(input), input);
}

TEST(ExtractJsonObjectTest, MarkdownFenceWithNonJsonLanguageTag) {
    EXPECT_EQ(ExtractJsonObject("```python\n{\"a\":1}\n```"), R"({"a":1})");
}

TEST(ExtractJsonArrayTest, ArrayWrappedInMarkdownFence) {
    EXPECT_EQ(ExtractJsonArray("```yaml\n[\"a\",\"b\"]\n```"), R"(["a","b"])");
}

TEST(ExtractJsonObjectTest, DeeplyNestedObjectKeepsFullSpan) {
    std::string nested = R"({"a":{"b":{"c":{"d":1}}}})";
    EXPECT_EQ(ExtractJsonObject(nested), nested);
}

TEST(ExtractJsonObjectTest, UnicodeContentPreserved) {
    // Literal UTF-8 bytes (accented "e" + a star emoji) inside a string
    // value, spelled out as explicit byte escapes so the test doesn't
    // depend on source-file or compiler execution-charset encoding.
    // The extractor operates on raw bytes, so multi-byte UTF-8 sequences
    // pass through untouched.
    std::string raw = "{\"msg\":\"h\xC3\xA9llo \xF0\x9F\x8C\x9F world\"}";
    EXPECT_EQ(ExtractJsonObject(raw), raw);
}

TEST(ExtractJsonObjectTest, BracesInsideStringValueDoNotBreakExtraction) {
    // The extractor is not brace-aware inside string literals; here the
    // last '}' in the text still happens to be the true closing brace,
    // so extraction succeeds despite the embedded '{' and '}'.
    std::string raw = R"({"content":"int main() { return 0; }"})";
    EXPECT_EQ(ExtractJsonObject(raw), raw);
}

TEST(ExtractJsonObjectTest, WhitespaceOnlyInputReturnsEmpty) {
    EXPECT_EQ(ExtractJsonObject("   \n\t  "), "");
}

TEST(ExtractJsonArrayTest, WhitespaceOnlyInputReturnsEmpty) {
    EXPECT_EQ(ExtractJsonArray("  \r\n  "), "");
}

TEST(ExtractJsonObjectTest, TrailingGarbageContainingStrayBraceExtendsSpan) {
    // Known limitation: a stray '}' anywhere after the real object --
    // even in unrelated trailing prose -- gets swept into the span
    // because the function just searches for the last '}' in the whole
    // string, not the one matching the first '{'.
    std::string raw = R"({"a":1} some trailing note with a } stray brace)";
    EXPECT_EQ(ExtractJsonObject(raw), R"({"a":1} some trailing note with a })");
}

TEST(ExtractJsonArrayTest, TrailingGarbageContainingStrayBracketExtendsSpan) {
    std::string raw = R"(["a","b"] some trailing note with a ] stray bracket)";
    EXPECT_EQ(ExtractJsonArray(raw), R"(["a","b"] some trailing note with a ])");
}

TEST(ExtractJsonArrayTest, ArrayOfObjectsKeepsOutermostBrackets) {
    std::string raw = R"([{"a":1},{"b":2}])";
    EXPECT_EQ(ExtractJsonArray(raw), raw);
}

TEST(ExtractJsonObjectTest, OnArrayOfObjectsGrabsSpanBetweenFirstAndLastBrace) {
    // Surprising: calling ExtractJsonObject on an array-of-objects finds
    // the first '{' (inside the array) and the last '}' (the final
    // object's close), yielding a comma-joined, bracket-less, invalid
    // JSON fragment rather than empty or the full array.
    std::string raw = R"([{"a":1},{"b":2}])";
    EXPECT_EQ(ExtractJsonObject(raw), R"({"a":1},{"b":2})");
}

TEST(ExtractJsonObjectTest, EscapedQuotesAndBackslashesPreserved) {
    std::string raw = R"({"path":"C:\\Users\\test","note":"she said \"hi\""})";
    EXPECT_EQ(ExtractJsonObject(raw), raw);
}

TEST(ExtractJsonObjectTest, LargeInputWithFarApartBracesExtractsFullSpan) {
    std::string prefix(50000, 'x');
    std::string suffix(50000, 'y');
    std::string object = R"({"key":"value"})";
    std::string raw = prefix + object + suffix;
    EXPECT_EQ(ExtractJsonObject(raw), object);
}

TEST(ExtractJsonObjectTest, WindowsCrlfLineEndingsPreserved) {
    std::string raw = "Explanation:\r\n\r\n{\r\n  \"a\": 1\r\n}\r\n\r\nDone.";
    EXPECT_EQ(ExtractJsonObject(raw), "{\r\n  \"a\": 1\r\n}");
}

TEST(ExtractJsonObjectTest, CodeExampleBeforeRealAnswerSpansBoth) {
    // The object of interest is NOT the first '{' in the text -- a code
    // example shown earlier also contains braces, and since the function
    // has no notion of "the right" object, it spans from the example's
    // first '{' all the way to the real answer's closing '}'.
    std::string raw = "Example: `if (x) { return; }`\n\nHere is the JSON: {\"result\":42}";
    std::string expected = "{ return; }`\n\nHere is the JSON: {\"result\":42}";
    EXPECT_EQ(ExtractJsonObject(raw), expected);
}

TEST(ExtractJsonArrayTest, CodeExampleBeforeRealAnswerSpansBoth) {
    std::string raw = "Example: `arr[0] = 1;` Now the tags: [\"a\",\"b\"]";
    std::string expected = "[0] = 1;` Now the tags: [\"a\",\"b\"]";
    EXPECT_EQ(ExtractJsonArray(raw), expected);
}

TEST(JsonUtilCombinedTest, ObjectAndArrayBothPresentExtractedIndependently) {
    std::string raw = R"(Tags: ["a","b"] Data: {"x":1})";
    EXPECT_EQ(ExtractJsonObject(raw), R"({"x":1})");
    EXPECT_EQ(ExtractJsonArray(raw), R"(["a","b"])");
}
