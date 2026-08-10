#include "cppcoder/FactExtractor.h"

#include <gtest/gtest.h>

using cppcoder::ExtractFacts;

TEST(FactExtractorTest, ExtractsNameWithIs) {
    auto facts = ExtractFacts("My name is Christian, nice to meet you.");
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The user's name is Christian.");
}

TEST(FactExtractorTest, ExtractsNameWithoutIs) {
    auto facts = ExtractFacts("My name Christian");
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The user's name is Christian.");
}

TEST(FactExtractorTest, ExtractsAssistantName) {
    auto facts = ExtractFacts("Your name is Charlie from now on.");
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The assistant should be called Charlie.");
}

TEST(FactExtractorTest, ExtractsAgeWithYoSuffix) {
    auto facts = ExtractFacts("I am 55yo and I like C++.");
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The user is 55 years old.");
}

TEST(FactExtractorTest, ExtractsAgeWithYearsOld) {
    auto facts = ExtractFacts("I'm 42 years old.");
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The user is 42 years old.");
}

TEST(FactExtractorTest, ExtractsMultipleFactsFromOneMessage) {
    auto facts = ExtractFacts("My name is Christian and I am 55yo.");
    ASSERT_EQ(facts.size(), 2u);
    EXPECT_EQ(facts[0], "The user's name is Christian.");
    EXPECT_EQ(facts[1], "The user is 55 years old.");
}

TEST(FactExtractorTest, PlainQuestionExtractsNothing) {
    auto facts = ExtractFacts("How does the judge prune directions?");
    EXPECT_TRUE(facts.empty());
}

TEST(FactExtractorTest, EmptyMessageExtractsNothing) {
    auto facts = ExtractFacts("");
    EXPECT_TRUE(facts.empty());
}

TEST(FactExtractorTest, NameAllCapsVariantMatchesCaseInsensitively) {
    auto facts = ExtractFacts("MY NAME IS CHRISTIAN, nice to meet you.");
    ASSERT_EQ(facts.size(), 1u);
    // The regex is case-insensitive but the capture is copied verbatim,
    // so the captured name keeps whatever casing appeared in the message.
    EXPECT_EQ(facts[0], "The user's name is CHRISTIAN.");
}

TEST(FactExtractorTest, NameMixedCaseVariantMatches) {
    auto facts = ExtractFacts("my Name Is christian, how are you?");
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The user's name is christian.");
}

TEST(FactExtractorTest, NameApostropheSContractionMatches) {
    auto facts = ExtractFacts("my name's Christian, nice to meet you.");
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The user's name is Christian.");
}

TEST(FactExtractorTest, NameWithHyphenAndApostropheVariantsMatch) {
    auto hyphenated = ExtractFacts("my name is Mary-Jane and I love painting.");
    ASSERT_EQ(hyphenated.size(), 1u);
    EXPECT_EQ(hyphenated[0], "The user's name is Mary-Jane.");

    auto apostrophe = ExtractFacts("my name is O'Brien, pleased to meet you.");
    ASSERT_EQ(apostrophe.size(), 1u);
    EXPECT_EQ(apostrophe[0], "The user's name is O'Brien.");
}

TEST(FactExtractorTest, DoubleSpaceBetweenMyAndNameDoesNotMatch) {
    // The pattern requires a single literal space between "my" and
    // "name"; a doubled space breaks the literal match even though the
    // surrounding "is"/name portion tolerates flexible whitespace.
    auto facts = ExtractFacts("my  name  is  Christian");
    EXPECT_TRUE(facts.empty());
}

TEST(FactExtractorTest, AgeAsDigitsWithYearsOldMatches) {
    auto facts = ExtractFacts("I am 30 years old and I love hiking.");
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The user is 30 years old.");
}

TEST(FactExtractorTest, SpelledOutAgeIsNotSupported) {
    auto facts = ExtractFacts("I'm thirty years old.");
    EXPECT_TRUE(facts.empty());
}

TEST(FactExtractorTest, AgeWithUnusualSpacingStillMatches) {
    auto facts = ExtractFacts("I am  55  yo, nice to meet you.");
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The user is 55 years old.");
}

TEST(FactExtractorTest, PlainConversationalMessageExtractsNothing) {
    auto facts = ExtractFacts("Thanks for the help earlier, that recipe turned out great!");
    EXPECT_TRUE(facts.empty());
}

TEST(FactExtractorTest, QuestionAboutNameIsNotFalselyExtracted) {
    // "What is my name?" contains the trigger words but is a question
    // asking for the name, not a statement of it, and the "my name"
    // literal requires something after it to capture -- it should not
    // be mistaken for a fact.
    auto facts = ExtractFacts("What is my name?");
    EXPECT_TRUE(facts.empty());
}

TEST(FactExtractorTest, ThreeDistinctFactsInOneMessageAllExtracted) {
    auto facts = ExtractFacts("My name is Christian. Your name is Charlie. I am 55yo.");
    ASSERT_EQ(facts.size(), 3u);
    EXPECT_EQ(facts[0], "The user's name is Christian.");
    EXPECT_EQ(facts[1], "The assistant should be called Charlie.");
    EXPECT_EQ(facts[2], "The user is 55 years old.");
}

TEST(FactExtractorTest, RepeatedAgeFactOnlyFirstOccurrenceReturned) {
    // Each pattern is searched for at most once per message, so a second,
    // contradictory age clause later in the same message is ignored.
    auto facts = ExtractFacts("I am 55yo and I am 60yo too.");
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The user is 55 years old.");
}

TEST(FactExtractorTest, PunctuationAndEmojiAroundFactDoNotInterfere) {
    auto facts = ExtractFacts("Hi!! My name is Christian!!! :) \xF0\x9F\x8C\x9F");
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The user's name is Christian.");
}

TEST(FactExtractorTest, FactBuriedInLongUnrelatedMessageIsFound) {
    std::string message =
        "This is a long rambling message that talks about the weather, "
        "the state of the economy, and several unrelated hobbies, and "
        "eventually, buried deep in the middle of it all, my name is "
        "Christian, before continuing on with even more unrelated text "
        "just to pad the message out as long as possible.";
    auto facts = ExtractFacts(message);
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The user's name is Christian.");
}

TEST(FactExtractorTest, LeadingAndTrailingWhitespaceAroundMessageIsIgnored) {
    auto facts = ExtractFacts("   My name is Christian.   ");
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The user's name is Christian.");
}

TEST(FactExtractorTest, WhitespaceOnlyMessageExtractsNothing) {
    auto facts = ExtractFacts("   \t\n   ");
    EXPECT_TRUE(facts.empty());
}

TEST(FactExtractorTest, UnicodeNameIsTruncatedAtNonAsciiByte) {
    // std::regex's default "C" locale classifies \w as ASCII word
    // characters only, so a multi-byte UTF-8 sequence (the accented
    // "c" in "Francois", spelled out below as explicit byte escapes so
    // this test doesn't depend on source-file encoding) stops the
    // capture group early rather than matching the whole name.
    auto facts = ExtractFacts("my name is Fran\xC3\xA7ois, from Paris.");
    ASSERT_EQ(facts.size(), 1u);
    EXPECT_EQ(facts[0], "The user's name is Fran.");
}

TEST(FactExtractorTest, AssistantNameCaseVariantMixedWithNameFactPreservesPatternOrder) {
    // The assistant-name phrase appears before the user-name phrase in
    // the message, but facts are emitted in pattern-definition order
    // (name, then assistant, then age), not in the order they occur in
    // the text.
    auto facts = ExtractFacts("YOUR NAME IS CHARLIE and my name is Christian.");
    ASSERT_EQ(facts.size(), 2u);
    EXPECT_EQ(facts[0], "The user's name is Christian.");
    EXPECT_EQ(facts[1], "The assistant should be called CHARLIE.");
}
