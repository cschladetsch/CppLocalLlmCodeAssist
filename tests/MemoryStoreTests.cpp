#include "cppcoder/MemoryStore.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;
using cppcoder::MemoryStore;

namespace {

// setenv/unsetenv are POSIX-only; MSVC's CRT doesn't provide them.
void SetEnvVar(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    ::setenv(name, value.c_str(), 1);
#endif
}

// Plain getenv is flagged deprecated by MSVC's CRT (it wants _dupenv_s,
// which side-steps the shared-buffer thread-safety concern getenv has).
std::optional<std::string> GetEnvVar(const char* name) {
#if defined(_WIN32)
    char* buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) return std::nullopt;
    std::string value(buffer);
    free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value ? std::optional<std::string>(value) : std::nullopt;
#endif
}

void UnsetEnvVar(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

class MemoryStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = (fs::temp_directory_path() / "cppcoder_memory_test" / "memory.json").string();
        std::error_code ec;
        fs::remove_all(fs::path(path_).parent_path(), ec);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(fs::path(path_).parent_path(), ec);
    }

    std::string path_;
};

}  // namespace

TEST_F(MemoryStoreTest, StartsEmptyWhenNoFileExists) {
    MemoryStore store(path_);
    EXPECT_TRUE(store.AllFacts().empty());
}

TEST_F(MemoryStoreTest, AddFactPersistsAndIsRetrievable) {
    MemoryStore store(path_);
    EXPECT_TRUE(store.AddFact("The user's name is Christian."));
    ASSERT_EQ(store.AllFacts().size(), 1u);
    EXPECT_EQ(store.AllFacts()[0], "The user's name is Christian.");
    EXPECT_TRUE(fs::exists(path_));
}

TEST_F(MemoryStoreTest, AddFactRejectsEmptyOrWhitespace) {
    MemoryStore store(path_);
    EXPECT_FALSE(store.AddFact(""));
    EXPECT_FALSE(store.AddFact("   "));
    EXPECT_TRUE(store.AllFacts().empty());
}

TEST_F(MemoryStoreTest, AddFactDedupesCaseInsensitively) {
    MemoryStore store(path_);
    EXPECT_TRUE(store.AddFact("The user is 55 years old."));
    EXPECT_FALSE(store.AddFact("the user is 55 years old."));
    EXPECT_FALSE(store.AddFact("THE USER IS 55 YEARS OLD."));
    EXPECT_EQ(store.AllFacts().size(), 1u);
}

TEST_F(MemoryStoreTest, RemoveFactDeletesMatchingEntry) {
    MemoryStore store(path_);
    store.AddFact("Fact one.");
    store.AddFact("Fact two.");
    EXPECT_TRUE(store.RemoveFact("fact one."));
    ASSERT_EQ(store.AllFacts().size(), 1u);
    EXPECT_EQ(store.AllFacts()[0], "Fact two.");
}

TEST_F(MemoryStoreTest, RemoveFactReturnsFalseWhenNotFound) {
    MemoryStore store(path_);
    store.AddFact("Fact one.");
    EXPECT_FALSE(store.RemoveFact("nonexistent fact"));
    EXPECT_EQ(store.AllFacts().size(), 1u);
}

TEST_F(MemoryStoreTest, FactsPersistAcrossInstances) {
    {
        MemoryStore store(path_);
        store.AddFact("The user's name is Christian.");
        store.AddFact("The user is 55 years old.");
    }
    MemoryStore reopened(path_);
    ASSERT_EQ(reopened.AllFacts().size(), 2u);
    EXPECT_EQ(reopened.AllFacts()[0], "The user's name is Christian.");
    EXPECT_EQ(reopened.AllFacts()[1], "The user is 55 years old.");
}

TEST(MemoryStoreDefaultPathTest, PrefersExplicitOverrideEnvVar) {
    const std::string override_path =
        (fs::temp_directory_path() / "cppcoder_memory_override.json").string();
    SetEnvVar("CPPCODER_MEMORY_FILE", override_path);
    EXPECT_EQ(MemoryStore::ResolveDefaultPath(), override_path);
    UnsetEnvVar("CPPCODER_MEMORY_FILE");
}

TEST(MemoryStoreDefaultPathTest, FallsBackToModelHomeWhenSet) {
    UnsetEnvVar("CPPCODER_MEMORY_FILE");
    const std::string model_home = (fs::temp_directory_path() / "cppcoder_model_home").string();
    SetEnvVar("DEEPSEEK_MODEL_HOME", model_home);
    EXPECT_EQ(MemoryStore::ResolveDefaultPath(), (fs::path(model_home) / "memory.json").string());
    UnsetEnvVar("DEEPSEEK_MODEL_HOME");
}

TEST_F(MemoryStoreTest, AddFactTrimsWhitespaceBeforeStoringAndDedupeComparison) {
    MemoryStore store(path_);
    ASSERT_TRUE(store.AddFact("  \t Fact Padded.  \n"));
    ASSERT_EQ(store.AllFacts().size(), 1u);
    // Only leading/trailing whitespace is stripped; internal spacing is untouched.
    EXPECT_EQ(store.AllFacts()[0], "Fact Padded.");

    // A second add that only differs by outer whitespace and casing should be
    // recognized as a duplicate once trimmed and lower-cased.
    EXPECT_FALSE(store.AddFact("   fact padded.   "));
    EXPECT_EQ(store.AllFacts().size(), 1u);
}

TEST_F(MemoryStoreTest, AddFactWhitespaceOnlyReturnsFalseAndAddsNothing) {
    MemoryStore store(path_);
    EXPECT_FALSE(store.AddFact("\t\r\n \t"));
    EXPECT_TRUE(store.AllFacts().empty());
    // Nothing was ever added, so no save should have occurred.
    EXPECT_FALSE(fs::exists(path_));
}

TEST_F(MemoryStoreTest, AddFactDuplicateWithDifferentCasingPreservesOriginalCasing) {
    MemoryStore store(path_);
    ASSERT_TRUE(store.AddFact("The Cat Sat."));
    EXPECT_FALSE(store.AddFact("the cat sat."));
    ASSERT_EQ(store.AllFacts().size(), 1u);
    // The originally-stored casing is kept; the duplicate attempt does not
    // overwrite it.
    EXPECT_EQ(store.AllFacts()[0], "The Cat Sat.");
}

TEST_F(MemoryStoreTest, RemoveFactMatchesDifferentCasingThanStored) {
    MemoryStore store(path_);
    ASSERT_TRUE(store.AddFact("Original Fact."));
    EXPECT_TRUE(store.RemoveFact("ORIGINAL fact."));
    EXPECT_TRUE(store.AllFacts().empty());
}

TEST_F(MemoryStoreTest, RemoveFactNeverAddedReturnsFalseAndLeavesFactsUnchanged) {
    MemoryStore store(path_);
    store.AddFact("Fact Alpha.");
    store.AddFact("Fact Beta.");
    EXPECT_FALSE(store.RemoveFact("Fact Gamma."));
    ASSERT_EQ(store.AllFacts().size(), 2u);
    EXPECT_EQ(store.AllFacts()[0], "Fact Alpha.");
    EXPECT_EQ(store.AllFacts()[1], "Fact Beta.");
}

TEST_F(MemoryStoreTest, NewStoreAtNonexistentPathStartsEmptyAndCreatesFileOnFirstAdd) {
    // SetUp() already removed the parent directory of path_, so it does not
    // exist yet when the store is constructed.
    MemoryStore store(path_);
    EXPECT_FALSE(fs::exists(path_));
    EXPECT_TRUE(store.AllFacts().empty());

    store.AddFact("New Fact.");
    EXPECT_TRUE(fs::exists(path_));
}

TEST_F(MemoryStoreTest, SecondLiveInstanceAtSamePathPicksUpPersistedFacts) {
    MemoryStore store1(path_);
    store1.AddFact("Shared Fact.");

    // store1 is still alive; a second instance opened against the same path
    // should still see what was already persisted to disk.
    MemoryStore store2(path_);
    ASSERT_EQ(store2.AllFacts().size(), 1u);
    EXPECT_EQ(store2.AllFacts()[0], "Shared Fact.");
}

TEST_F(MemoryStoreTest, AllFactsPreservesInsertionOrderAcrossMultipleAdds) {
    MemoryStore store(path_);
    store.AddFact("First.");
    store.AddFact("Second.");
    store.AddFact("Third.");
    store.AddFact("Fourth.");
    store.AddFact("Fifth.");

    const std::vector<std::string> expected = {"First.", "Second.", "Third.", "Fourth.",
                                                "Fifth."};
    EXPECT_EQ(store.AllFacts(), expected);
}

TEST_F(MemoryStoreTest, AddingManyFactsPreservesCountAndOrder) {
    MemoryStore store(path_);
    constexpr int kCount = 12;
    for (int i = 0; i < kCount; ++i) {
        ASSERT_TRUE(store.AddFact("Fact number " + std::to_string(i) + "."));
    }

    const auto facts = store.AllFacts();
    ASSERT_EQ(facts.size(), static_cast<size_t>(kCount));
    for (int i = 0; i < kCount; ++i) {
        EXPECT_EQ(facts[static_cast<size_t>(i)], "Fact number " + std::to_string(i) + ".");
    }
}

TEST_F(MemoryStoreTest, RemoveFactFromMiddlePreservesRelativeOrderOfRest) {
    MemoryStore store(path_);
    store.AddFact("Alpha.");
    store.AddFact("Beta.");
    store.AddFact("Gamma.");
    store.AddFact("Delta.");
    store.AddFact("Epsilon.");

    EXPECT_TRUE(store.RemoveFact("Gamma."));

    const std::vector<std::string> expected = {"Alpha.", "Beta.", "Delta.", "Epsilon."};
    EXPECT_EQ(store.AllFacts(), expected);
}

TEST_F(MemoryStoreTest, PathReturnsExactConstructorArgument) {
    MemoryStore store(path_);
    EXPECT_FALSE(store.path().empty());
    EXPECT_EQ(store.path(), path_);
}

TEST(MemoryStoreDefaultPathTest, PrefersOverrideEvenWhenModelHomeAlsoSet) {
    const std::optional<std::string> orig_override = GetEnvVar("CPPCODER_MEMORY_FILE");
    const std::optional<std::string> orig_model_home = GetEnvVar("DEEPSEEK_MODEL_HOME");
    const bool had_override = orig_override.has_value();
    const bool had_model_home = orig_model_home.has_value();

    const std::string override_path =
        (fs::temp_directory_path() / "cppcoder_memory_override_priority.json").string();
    const std::string model_home = (fs::temp_directory_path() / "cppcoder_model_home_priority").string();
    SetEnvVar("CPPCODER_MEMORY_FILE", override_path);
    SetEnvVar("DEEPSEEK_MODEL_HOME", model_home);

    EXPECT_EQ(MemoryStore::ResolveDefaultPath(), override_path);

    if (had_override) {
        SetEnvVar("CPPCODER_MEMORY_FILE", *orig_override);
    } else {
        UnsetEnvVar("CPPCODER_MEMORY_FILE");
    }
    if (had_model_home) {
        SetEnvVar("DEEPSEEK_MODEL_HOME", *orig_model_home);
    } else {
        UnsetEnvVar("DEEPSEEK_MODEL_HOME");
    }
}

TEST(MemoryStoreDefaultPathTest, FallsBackToDotModelsMemoryJsonWhenNeitherEnvVarSet) {
    const std::optional<std::string> orig_override = GetEnvVar("CPPCODER_MEMORY_FILE");
    const std::optional<std::string> orig_model_home = GetEnvVar("DEEPSEEK_MODEL_HOME");
    const bool had_override = orig_override.has_value();
    const bool had_model_home = orig_model_home.has_value();

    UnsetEnvVar("CPPCODER_MEMORY_FILE");
    UnsetEnvVar("DEEPSEEK_MODEL_HOME");

    const fs::path resolved = fs::path(MemoryStore::ResolveDefaultPath());
    EXPECT_EQ(resolved.filename().string(), "memory.json");
    EXPECT_EQ(resolved.parent_path().filename().string(), ".models");

    if (had_override) {
        SetEnvVar("CPPCODER_MEMORY_FILE", *orig_override);
    } else {
        UnsetEnvVar("CPPCODER_MEMORY_FILE");
    }
    if (had_model_home) {
        SetEnvVar("DEEPSEEK_MODEL_HOME", *orig_model_home);
    } else {
        UnsetEnvVar("DEEPSEEK_MODEL_HOME");
    }
}
