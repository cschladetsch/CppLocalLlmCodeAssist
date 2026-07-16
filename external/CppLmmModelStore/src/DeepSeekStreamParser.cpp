#include "DeepSeekStreamParser.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <string>

namespace deepseek {
namespace {

bool ExtractDelta(const nlohmann::json& j, std::string* reasoning, std::string* content) {
  if (!j.contains("choices") || j["choices"].empty()) {
    return false;
  }
  const auto& delta = j["choices"][0].value("delta", nlohmann::json::object());
  if (delta.contains("reasoning_content")) {
    *reasoning = delta.value("reasoning_content", "");
  }
  if (delta.contains("content")) {
    *content = delta.value("content", "");
  }
  return !reasoning->empty() || !content->empty();
}

}  // namespace

DeepSeekStreamParser::DeepSeekStreamParser(DeltaCallback on_delta)
    : on_delta_(std::move(on_delta)) {}

bool DeepSeekStreamParser::Feed(std::string_view chunk, std::string* error_out) {
  buffer_.append(chunk.data(), chunk.size());

  size_t pos = 0;
  while (true) {
    const size_t nl = buffer_.find('\n', pos);
    if (nl == std::string::npos) {
      buffer_.erase(0, pos);
      break;
    }
    std::string line = buffer_.substr(pos, nl - pos);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    pos = nl + 1;

    if (line.rfind("data:", 0) != 0) {
      continue;
    }
    std::string payload = line.substr(5);
    if (!payload.empty() && payload.front() == ' ') {
      payload.erase(0, 1);
    }
    if (payload == "[DONE]") {
      continue;
    }

    nlohmann::json j;
    try {
      j = nlohmann::json::parse(payload);
    } catch (const std::exception& ex) {
      std::string message = std::string("Invalid JSON in stream: ") + ex.what();
      spdlog::warn("DeepSeekStreamParser: {}", message);
      if (error_out) {
        *error_out = std::move(message);
      }
      return false;
    }

    std::string reasoning_delta;
    std::string content_delta;
    if (ExtractDelta(j, &reasoning_delta, &content_delta)) {
      spdlog::trace("DeepSeekStreamParser: delta reasoning={} bytes, content={} bytes",
                    reasoning_delta.size(), content_delta.size());
      on_delta_(reasoning_delta, content_delta);
    }
  }
  return true;
}

}  // namespace deepseek
