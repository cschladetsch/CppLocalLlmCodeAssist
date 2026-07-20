#include "cppcoder/ChatServer.h"

#include "cppcoder/FactExtractor.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <string>

namespace {

bool WriteNdjsonError(httplib::DataSink& sink, const std::string& message) {
    nlohmann::json err{{"error", message}};
    std::string line = err.dump() + "\n";
    return sink.write(line.data(), line.size());
}

}  // namespace

namespace cppcoder {

using json = nlohmann::json;

ChatServer::ChatServer(ChatServerConfig config)
    : config_(std::move(config)), memory_(config_.memoryFilePath) {}

int ChatServer::Run() {
    httplib::Server svr;

    if (!config_.webRoot.empty()) {
        if (!svr.set_mount_point("/", config_.webRoot)) {
            spdlog::warn(
                "ChatServer: could not mount web root '{}' -- static UI won't be served",
                config_.webRoot);
        }
    }

    // GET /api/models -- model tags Ollama currently has pulled locally,
    // for the chat UI's model-switcher dropdown.
    svr.Get("/api/models", [this](const httplib::Request&, httplib::Response& res) {
        httplib::Client ollama(config_.ollamaHost, config_.ollamaPort);
        ollama.set_connection_timeout(5, 0);
        auto ollamaRes = ollama.Get("/api/tags");

        json body;
        body["default"] = config_.defaultModel;
        body["models"] = json::array();

        if (ollamaRes && ollamaRes->status == 200) {
            try {
                json parsed = json::parse(ollamaRes->body);
                for (const auto& m : parsed.value("models", json::array())) {
                    body["models"].push_back(m.value("name", std::string{}));
                }
            } catch (const json::exception& e) {
                spdlog::error("ChatServer: failed to parse /api/tags response: {}", e.what());
            }
        } else {
            res.status = 502;
            body["error"] =
                "Could not reach Ollama at " + config_.ollamaHost + ":" +
                std::to_string(config_.ollamaPort) +
                " -- is it running? (`ollama serve`)";
        }
        res.set_content(body.dump(), "application/json");
    });

    // GET /api/memory -- list every fact remembered about the user so far.
    svr.Get("/api/memory", [this](const httplib::Request&, httplib::Response& res) {
        json body;
        body["facts"] = memory_.AllFacts();
        res.set_content(body.dump(), "application/json");
    });

    // POST /api/memory -- manually add a fact (body: {"fact": "..."}),
    // for corrections or facts the auto-extractor wouldn't catch.
    svr.Post("/api/memory", [this](const httplib::Request& req, httplib::Response& res) {
        json in;
        try {
            in = json::parse(req.body);
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", std::string("invalid JSON body: ") + e.what()}}.dump(),
                             "application/json");
            return;
        }
        memory_.AddFact(in.value("fact", std::string{}));
        json body;
        body["facts"] = memory_.AllFacts();
        res.set_content(body.dump(), "application/json");
    });

    // DELETE /api/memory -- forget a fact (body: {"fact": "..."}), exact
    // match case-insensitively.
    svr.Delete("/api/memory", [this](const httplib::Request& req, httplib::Response& res) {
        json in;
        try {
            in = json::parse(req.body);
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", std::string("invalid JSON body: ") + e.what()}}.dump(),
                             "application/json");
            return;
        }
        memory_.RemoveFact(in.value("fact", std::string{}));
        json body;
        body["facts"] = memory_.AllFacts();
        res.set_content(body.dump(), "application/json");
    });

    // POST /api/chat -- plain conversational chat, no research engine
    // involved. Proxies straight through to Ollama's own /api/chat and
    // streams the newline-delimited JSON response back to the browser as
    // it arrives, so the reply appears token-by-token like a normal chat
    // client. Body in: {"model": "...", "messages": [{"role","content"}]}.
    //
    // Also where remembered facts get read and written: the latest user
    // message is scanned for new facts (FactExtractor) before the request
    // goes out, and every fact known so far is prepended as a system
    // message so the assistant has them regardless of which model is
    // selected or whether this is a brand new conversation.
    svr.Post("/api/chat", [this](const httplib::Request& req, httplib::Response& res) {
        json in;
        try {
            in = json::parse(req.body);
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", std::string("invalid JSON body: ") + e.what()}}.dump(),
                             "application/json");
            return;
        }

        json messages = in.value("messages", json::array());

        if (!messages.empty() && messages.back().value("role", std::string{}) == "user") {
            std::string latest = messages.back().value("content", std::string{});
            for (const auto& fact : ExtractFacts(latest)) {
                memory_.AddFact(fact);
            }
        }

        auto facts = memory_.AllFacts();
        if (!facts.empty()) {
            std::string systemContent =
                "Known facts about the user, remembered from earlier conversations:\n";
            for (const auto& fact : facts) {
                systemContent += "- " + fact + "\n";
            }
            messages.insert(messages.begin(), json{{"role", "system"}, {"content", systemContent}});
        }

        json out;
        out["model"] = in.value("model", config_.defaultModel);
        out["messages"] = messages;
        out["stream"] = true;

        res.set_chunked_content_provider(
            "application/x-ndjson",
            [this, out](size_t offset, httplib::DataSink& sink) -> bool {
                // Everything happens on the first (and only) call: we make
                // a blocking streaming POST to Ollama here, forwarding
                // every chunk straight to the client's sink as it arrives.
                // Returning false afterwards tells httplib the body is
                // complete; it never calls this provider again.
                if (offset != 0) {
                    return false;
                }

                httplib::Client ollama(config_.ollamaHost, config_.ollamaPort);
                ollama.set_read_timeout(60, 0);
                ollama.set_write_timeout(600, 0);
                ollama.set_connection_timeout(10, 0);

                httplib::Request chatReq;
                chatReq.method = "POST";
                chatReq.path = "/api/chat";
                chatReq.set_header("Content-Type", "application/json");
                chatReq.body = out.dump();
                bool forwardedError = false;
                chatReq.response_handler = [&sink, &forwardedError](const httplib::Response& r) {
                    if (r.status >= 200 && r.status < 300) {
                        return true;
                    }

                    WriteNdjsonError(sink, "Ollama returned HTTP " + std::to_string(r.status));
                    forwardedError = true;
                    return false;
                };
                chatReq.content_receiver = [&sink](const char* data, size_t len, uint64_t,
                                                    uint64_t) -> bool {
                    return sink.write(data, len);
                };

                auto chatRes = ollama.send(chatReq);
                if (!chatRes && !forwardedError) {
                    std::string message = chatRes.error() == httplib::Error::Read
                                              ? "Ollama did not send chat data for 60 seconds"
                                              : "Ollama request failed: " +
                                                    httplib::to_string(chatRes.error());
                    WriteNdjsonError(sink, message);
                    spdlog::error("ChatServer: /api/chat proxy failed: {}",
                                  httplib::to_string(chatRes.error()));
                }

                sink.done();
                return false;
            });
    });

    spdlog::info("Chat server listening on http://{}:{} (web root: {})", config_.bindHost,
                 config_.bindPort, config_.webRoot.empty() ? "<none>" : config_.webRoot);
    spdlog::info("Ollama backend: {}:{}, default model: {}", config_.ollamaHost,
                 config_.ollamaPort, config_.defaultModel);
    spdlog::info("Press Ctrl+C to stop.");

    if (!svr.listen(config_.bindHost, config_.bindPort)) {
        spdlog::error("ChatServer: failed to bind {}:{}", config_.bindHost, config_.bindPort);
        return 1;
    }
    return 0;
}

}  // namespace cppcoder
