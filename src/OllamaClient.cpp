#include "cppcoder/OllamaClient.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace cppcoder {

using json = nlohmann::json;

namespace {
std::string GetClientUrl(const std::string& host, int port) {
    std::string base = host;
    if (base.rfind("http://", 0) != 0 && base.rfind("https://", 0) != 0) {
        base = "http://" + base;
    }
    if (base.find(':', 8) == std::string::npos && port > 0) {
        base += ":" + std::to_string(port);
    }
    return base;
}
} // namespace

OllamaClient::OllamaClient(OllamaConfig config) : config_(std::move(config)) {}

std::optional<std::string> OllamaClient::Generate(const std::string& prompt,
                                                  const std::string& systemPrompt) const {
    std::string url = GetClientUrl(config_.host, config_.port);
    httplib::Client cli(url);
    cli.set_read_timeout(config_.timeoutSeconds, 0);
    cli.set_write_timeout(config_.timeoutSeconds, 0);
    cli.set_connection_timeout(10, 0);

    json options = {{"temperature", config_.temperature}};
    if (config_.numCtx > 0) {
        options["num_ctx"] = config_.numCtx;
    }

    json body = {
        {"model", config_.model},
        {"prompt", prompt},
        {"stream", false},
        {"options", std::move(options)},
    };
    if (!systemPrompt.empty()) {
        body["system"] = systemPrompt;
    }

    auto res = cli.Post("/api/generate", body.dump(), "application/json");
    if (!res) {
        spdlog::error("OllamaClient: request failed, error={}", httplib::to_string(res.error()));
        return std::nullopt;
    }
    if (res->status != 200) {
        spdlog::error("OllamaClient: HTTP {}: {}", res->status, res->body);
        return std::nullopt;
    }

    try {
        json parsed = json::parse(res->body);
        return parsed.value("response", std::string{});
    } catch (const json::exception& e) {
        spdlog::error("OllamaClient: failed to parse response JSON: {}", e.what());
        return std::nullopt;
    }
}

bool OllamaClient::IsModelAvailable() const {
    std::string url = GetClientUrl(config_.host, config_.port);
    httplib::Client cli(url);
    cli.set_connection_timeout(5, 0);
    auto res = cli.Get("/api/tags");
    if (!res || res->status != 200) {
        return false;
    }
    try {
        json parsed = json::parse(res->body);
        for (const auto& m : parsed.value("models", json::array())) {
            if (m.value("name", std::string{}) == config_.model) {
                return true;
            }
        }
    } catch (const json::exception&) {
        return false;
    }
    return false;
}

bool OllamaClient::IsServerReachable() const {
    std::string url = GetClientUrl(config_.host, config_.port);
    httplib::Client cli(url);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/api/tags");
    return res && res->status == 200;
}

} // namespace cppcoder
