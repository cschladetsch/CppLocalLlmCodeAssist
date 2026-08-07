#include "cppcoder/ChatServer.h"

#include "cppcoder/FactExtractor.h"
#include "cppcoder/LocalCommands.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#define POPEN _popen
#define PCLOSE _pclose
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#define POPEN popen
#define PCLOSE pclose
#else
#include <sys/sysinfo.h>
#define POPEN popen
#define PCLOSE pclose
#endif

namespace {

bool WriteNdjsonError(httplib::DataSink& sink, const std::string& message) {
    nlohmann::json err{{"error", message}};
    std::string line = err.dump() + "\n";
    return sink.write(line.data(), line.size());
}

struct HardwareInfo {
    double ram_gb = 0.0;
    double vram_gb = 0.0;
    bool vram_detected = false;
};

double GetTotalRamGb() {
#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<double>(status.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
    }
    return 0.0;
#elif defined(__APPLE__)
    int64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0) == 0) {
        return static_cast<double>(memsize) / (1024.0 * 1024.0 * 1024.0);
    }
    return 0.0;
#else
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return static_cast<double>(info.totalram) * info.mem_unit / (1024.0 * 1024.0 * 1024.0);
    }
    return 0.0;
#endif
}

// NVIDIA only -- AMD/Intel GPUs fall back to vram_detected=false and get
// judged on RAM alone. Good enough for now; revisit if that matters.
double GetTotalVramGb(bool& detected) {
    detected = false;
    FILE* pipe = POPEN("nvidia-smi --query-gpu=memory.total --format=csv,noheader,nounits", "r");
    if (!pipe) return 0.0;
    char buffer[256] = {};
    std::string result;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result = buffer;
    }
    int status = PCLOSE(pipe);
    if (status != 0 || result.empty()) return 0.0;
    try {
        double mib = std::stod(result);
        detected = true;
        return mib / 1024.0;  // MiB -> GiB
    } catch (...) {
        return 0.0;
    }
}

HardwareInfo DetectHardware() {
    HardwareInfo hw;
    hw.ram_gb = GetTotalRamGb();
    hw.vram_gb = GetTotalVramGb(hw.vram_detected);
    return hw;
}

struct ModelSuggestion {
    std::string name;
    double size_gb;       // approx download size, Q4_K_M quantization
    double min_ram_gb;    // rough comfortable minimum
    double min_vram_gb;   // rough comfortable minimum for full GPU offload
    std::string category; // "coder" (default suggestions) or "uncensored"
};

// Approximate figures for common quantizations -- worth double-checking
// against Ollama's library page if you add/change entries, since exact
// sizes vary by quant level.
//
// "uncensored" entries are general-purpose/roleplay models with reduced
// built-in content restrictions, kept in a separate category so the UI
// can gate them behind an explicit opt-in rather than mixing them into
// the default coder suggestions.
const std::vector<ModelSuggestion>& SuggestedModels() {
    static const std::vector<ModelSuggestion> kSuggestions = {
        {"qwen2.5-coder:1.5b",         1.0,  4.0,  2.0,  "coder"},
        {"qwen2.5-coder:7b",           4.7,  8.0,  6.0,  "coder"},
        {"qwen2.5-coder:14b",          9.0,  16.0, 10.0, "coder"},
        {"qwen2.5-coder:32b",          20.0, 32.0, 20.0, "coder"},
        {"deepseek-coder-v2:16b",      9.0,  16.0, 10.0, "coder"},
        {"codellama:7b",               3.8,  8.0,  6.0,  "coder"},
        {"codellama:13b",              7.4,  16.0, 10.0, "coder"},
        {"starcoder2:3b",              1.7,  4.0,  3.0,  "coder"},
        {"dolphin-mixtral:8x7b",       26.0, 48.0, 26.0, "uncensored"},
        {"wizard-vicuna-uncensored:13b", 7.4, 16.0, 10.0, "uncensored"},
        {"dolphin-llama3:8b",          4.7,  8.0,  6.0,  "uncensored"},
    };
    return kSuggestions;
}

}  // namespace

namespace cppcoder {

using json = nlohmann::json;

ChatServer::ChatServer(ChatServerConfig config)
    : config_(std::move(config)), memory_(config_.memoryFilePath) {}

int ChatServer::Run() {
    httplib::Server svr;

    const HardwareInfo hw = DetectHardware();
    spdlog::info("Detected hardware: {:.1f} GB RAM, {}", hw.ram_gb,
                 hw.vram_detected ? (std::to_string(static_cast<int>(hw.vram_gb)) + " GB VRAM")
                                  : "no NVIDIA GPU detected");

    // Sandbox root for the local /ls, /read and /write chat commands.
    // Resolved once here rather than per request so the reachable area
    // can't shift if something later changes the process cwd, and so a
    // "not in a repo" fallback is reported once at startup instead of
    // silently on every command.
    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::filesystem::path fileRoot = FindRepoRoot(cwd);
    // FindRepoRoot returns its argument on failure, and cwd may itself
    // be the repository root, so probe for .git rather than comparing.
    std::error_code rootEc;
    if (!std::filesystem::exists(fileRoot / ".git", rootEc)) {
        spdlog::warn(
            "ChatServer: no enclosing git repository found -- /ls, /read and /write are "
            "confined to the current directory '{}'",
            cwd.string());
    } else {
        spdlog::info("ChatServer: /ls, /read and /write are confined to the repository root '{}'",
                     fileRoot.string());
    }

    if (!config_.webRoot.empty()) {
        if (!svr.set_mount_point("/", config_.webRoot)) {
            spdlog::warn(
                "ChatServer: could not mount web root '{}' -- static UI won't be served",
                config_.webRoot);
        }
    }

    // GET /api/models -- model tags Ollama currently has pulled locally,
    // for the chat UI's model-switcher dropdown. Also returns a curated
    // list of suggested-but-not-pulled models with approximate download
    // sizes, and detected local hardware (RAM / NVIDIA VRAM) so the UI can
    // flag suggestions that likely won't fit.
    svr.Get("/api/models", [this, hw](const httplib::Request&, httplib::Response& res) {
        httplib::Client ollama(config_.ollamaHost, config_.ollamaPort);
        ollama.set_connection_timeout(5, 0);
        auto ollamaRes = ollama.Get("/api/tags");

        json body;
        body["default"] = config_.defaultModel;
        body["models"] = json::array();
        body["hardware"] = {
            {"ram_gb", hw.ram_gb},
            {"vram_gb", hw.vram_gb},
            {"vram_detected", hw.vram_detected},
        };

        std::vector<std::string> pulled;
        if (ollamaRes && ollamaRes->status == 200) {
            try {
                json parsed = json::parse(ollamaRes->body);
                for (const auto& m : parsed.value("models", json::array())) {
                    std::string name = m.value("name", std::string{});
                    pulled.push_back(name);
                    body["models"].push_back(name);
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

        body["suggestions"] = json::array();
        for (const auto& s : SuggestedModels()) {
            if (std::find(pulled.begin(), pulled.end(), s.name) != pulled.end()) {
                continue;  // already pulled -- no need to suggest it
            }
            bool fitsRam = hw.ram_gb <= 0.0 || hw.ram_gb >= s.min_ram_gb;
            bool fitsVram = !hw.vram_detected || hw.vram_gb >= s.min_vram_gb;
            body["suggestions"].push_back({
                {"name", s.name},
                {"size_gb", s.size_gb},
                {"min_ram_gb", s.min_ram_gb},
                {"min_vram_gb", s.min_vram_gb},
                {"fits", fitsRam && fitsVram},
                {"category", s.category},
            });
        }

        res.set_content(body.dump(), "application/json");
    });

    // POST /api/pull -- pull a model into Ollama (body: {"model": "..."}).
    // Proxies straight through to Ollama's own /api/pull and streams the
    // newline-delimited JSON progress events back to the browser as they
    // arrive, so the UI can show a live progress bar. Same streaming
    // pattern as /api/chat below.
    svr.Post("/api/pull", [this](const httplib::Request& req, httplib::Response& res) {
        json in;
        try {
            in = json::parse(req.body);
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", std::string("invalid JSON body: ") + e.what()}}.dump(),
                             "application/json");
            return;
        }

        std::string model = in.value("model", std::string{});
        if (model.empty()) {
            res.status = 400;
            res.set_content(json{{"error", "missing 'model'"}}.dump(), "application/json");
            return;
        }

        json out;
        out["model"] = model;
        out["stream"] = true;

        res.set_chunked_content_provider(
            "application/x-ndjson",
            [this, out](size_t offset, httplib::DataSink& sink) -> bool {
                if (offset != 0) {
                    return false;
                }

                httplib::Client ollama(config_.ollamaHost, config_.ollamaPort);
                ollama.set_read_timeout(60, 0);
                ollama.set_write_timeout(3600, 0);  // pulls can take a long time
                ollama.set_connection_timeout(10, 0);

                httplib::Request pullReq;
                pullReq.method = "POST";
                pullReq.path = "/api/pull";
                pullReq.set_header("Content-Type", "application/json");
                pullReq.body = out.dump();
                bool forwardedError = false;
                pullReq.response_handler = [&sink, &forwardedError](const httplib::Response& r) {
                    if (r.status >= 200 && r.status < 300) {
                        return true;
                    }
                    WriteNdjsonError(sink, "Ollama returned HTTP " + std::to_string(r.status));
                    forwardedError = true;
                    return false;
                };
                pullReq.content_receiver = [&sink](const char* data, size_t len, uint64_t,
                                                    uint64_t) -> bool {
                    return sink.write(data, len);
                };

                auto pullRes = ollama.send(pullReq);
                if (!pullRes && !forwardedError) {
                    std::string message = pullRes.error() == httplib::Error::Read
                                              ? "Ollama did not send pull progress for 60 seconds"
                                              : "Ollama pull request failed: " +
                                                    httplib::to_string(pullRes.error());
                    WriteNdjsonError(sink, message);
                    spdlog::error("ChatServer: /api/pull proxy failed: {}",
                                  httplib::to_string(pullRes.error()));
                }

                sink.done();
                return false;
            });
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
    //
    // Also intercepts local filesystem commands -- /pwd, /cwd, /ls
    // [path], /read <path>, /write <path>\n<content> -- and answers
    // them directly against `fileRoot` (the enclosing git checkout)
    // instead of forwarding to Ollama at all. See LocalCommands.h/.cpp
    // for the implementation and LocalCommandsTests.cpp for coverage.
    svr.Post("/api/chat", [this, fileRoot](const httplib::Request& req, httplib::Response& res) {
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
        std::string requestedModel = in.value("model", config_.defaultModel);

        // /pwd, /cwd, /ls, /read, /write: handled locally against the
        // repository root (LocalCommands.h) and never forwarded to
        // Ollama -- the model itself has no filesystem access.
        if (!messages.empty() && messages.back().value("role", std::string{}) == "user") {
            std::string latest = messages.back().value("content", std::string{});

            LocalCommandResult localResult = TryHandleLocalCommand(latest, fileRoot);
            if (localResult.handled) {
                res.set_chunked_content_provider(
                    "application/x-ndjson",
                    [requestedModel, text = localResult.text](size_t offset,
                                                                httplib::DataSink& sink) -> bool {
                        if (offset != 0) return false;
                        json chunk{
                            {"model", requestedModel},
                            {"message", {{"role", "assistant"}, {"content", text}}},
                            {"done", true},
                        };
                        std::string line = chunk.dump() + "\n";
                        sink.write(line.data(), line.size());
                        sink.done();
                        return false;
                    });
                return;
            }

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
		chatReq.content_receiver = [&sink](const char* data, size_t len, uint64_t, uint64_t) -> bool {
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
