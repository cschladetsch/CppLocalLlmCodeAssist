#include "ModelStore.hpp"

#include <cstdlib>
#include <filesystem>
#include <optional>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

// setenv/unsetenv are POSIX-only; MSVC's CRT (including under clang-cl)
// doesn't provide them at all. _putenv_s is the portable-on-Windows
// equivalent, and passing "" as the value removes the variable, matching
// unsetenv's effect -- ResolveModelHomeInternal() already treats an empty
// override as "not set" too, so behavior stays identical either way.
void SetEnvVar(const char* name, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(name, value.c_str());
#else
  ::setenv(name, value.c_str(), 1);
#endif
}

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

}  // namespace

TEST(ModelStoreTests, ResolveAndCreate) {
  const char* kEnv = "DEEPSEEK_MODEL_HOME";
  const std::string base = (fs::temp_directory_path() / "deepseek_models_test").string();
  SetEnvVar(kEnv, base);

  const std::string model = "deepseek-r1";
  const std::string expected = (fs::path(base) / model).string();
  EXPECT_EQ(deepseek::ModelStore::ResolveModelPath(model), expected);

  std::string error;
  auto created = deepseek::ModelStore::EnsureModelDir(model, &error);
  ASSERT_TRUE(created.has_value()) << error;
  EXPECT_TRUE(deepseek::ModelStore::ModelExists(model));

  std::error_code ec;
  fs::remove_all(base, ec);
  EXPECT_FALSE(ec);

  UnsetEnvVar(kEnv);
}

TEST(ModelStoreTests, SanitizesColonsInModelNameForWindowsCompatibility) {
  // Registry-style model tags (e.g. Ollama's "llama3:8b") contain ':',
  // which NTFS treats as alternate-data-stream syntax rather than a
  // normal path character. ResolveModelPath must sanitize it so the
  // resulting path is a real, creatable directory on every platform.
  const char* kEnv = "DEEPSEEK_MODEL_HOME";
  const std::string base = (fs::temp_directory_path() / "deepseek_models_test_colon").string();
  SetEnvVar(kEnv, base);

  const std::string model = "qwen2.5-coder:7b";
  const std::string expected = (fs::path(base) / "qwen2.5-coder_7b").string();
  EXPECT_EQ(deepseek::ModelStore::ResolveModelPath(model), expected);

  std::string error;
  auto created = deepseek::ModelStore::EnsureModelDir(model, &error);
  ASSERT_TRUE(created.has_value()) << error;
  EXPECT_TRUE(deepseek::ModelStore::ModelExists(model));

  std::error_code ec;
  fs::remove_all(base, ec);
  EXPECT_FALSE(ec);

  UnsetEnvVar(kEnv);
}

TEST(ModelStoreTests, DefaultsToDotModelsUnderUserHome) {
  const char* kEnv = "DEEPSEEK_MODEL_HOME";
  const std::optional<std::string> orig_model_home = GetEnvVar(kEnv);
  UnsetEnvVar(kEnv);

  const fs::path resolved = fs::path(deepseek::ModelStore::ResolveModelHome());
  EXPECT_EQ(resolved.filename().string(), ".models");

  if (orig_model_home) {
    SetEnvVar(kEnv, *orig_model_home);
  } else {
    UnsetEnvVar(kEnv);
  }
}
