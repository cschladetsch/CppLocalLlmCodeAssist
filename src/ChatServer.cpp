#include "cppcoder/ChatServer.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <string>

namespace cppcoder {

using json = nlohmann::json;

ChatServer::ChatServer(ChatServerConfig config) : config_(std::move(config)) {}

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

    // POST /api/chat -- plain conversational chat, no research engine
    // involved. Proxies straight through to Ollama's own /api/chat and
    // streams the newline-delimited JSON response back to the browser as
    // it arrives, so the reply appears token-by-token like a normal chat
    // client. Body in: {"model": "...", "messages": [{"role","content"}]}.
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

        json out;
        out["model"] = in.value("model", config_.defaultModel);
        out["messages"] = in.value("messages", json::array());
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
                ollama.set_read_timeout(600, 0);
                ollama.set_write_timeout(600, 0);
                ollama.set_connection_timeout(10, 0);

                httplib::Request chatReq;
                chatReq.method = "POST";
                chatReq.path = "/api/chat";
                chatReq.set_header("Content-Type", "application/json");
                chatReq.body = out.dump();
                chatReq.content_receiver = [&sink](const char* data, size_t len, uint64_t,
                                                    uint64_t) -> bool {
                    return sink.write(data, len);
                };

                auto chatRes = ollama.send(chatReq);
                if (!chatRes) {
                    json err{{"error", "Ollama request failed: " +
                                            httplib::to_string(chatRes.error())}};
                    std::string line = err.dump() + "\n";
                    sink.write(line.data(), line.size());
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
