#include "cppcoder/ChatCli.h"

#include "cppcoder/FactExtractor.h"
#include "cppcoder/FileRetriever.h"
#include "cppcoder/LocalCommands.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <rang.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace cppcoder {

using json = nlohmann::json;

ChatCli::ChatCli(ChatCliConfig config)
    : config_(std::move(config)), memory_(config_.memoryFilePath) {}

namespace {

// A model that just crashed loading (e.g. a transient CUDA OOM while
// another GPU app was mid-frame) often succeeds a few seconds later once
// that memory is released, so a fresh model-load failure is worth a
// short retry rather than an immediate error. A 4xx or a repeat failure
// isn't going to fix itself, so those still fail fast. 2xx counts too --
// this is only ever consulted when no content came back (see the
// retryable check in StreamChat), and a "successful" empty stream is
// exactly the kind of transient hiccup worth one more try rather than
// silently returning nothing.
bool IsRetryableStatus(int status) {
    return status == 0 || status >= 500 || (status >= 200 && status < 300);
}

// Prints a spinner on stderr until `waiting` goes false, then erases the
// line. Runs on its own thread because the request itself blocks the
// calling thread inside httplib::Client::send().
class Spinner {
public:
    explicit Spinner(const std::string& label)
        : thread_([this, label] {
              static constexpr char kFrames[] = {'|', '/', '-', '\\'};
              size_t i = 0;
              while (waiting_.load(std::memory_order_relaxed)) {
                  std::cerr << "\r" << rang::style::dim << label << " " << kFrames[i++ % 4]
                            << rang::style::reset << std::flush;
                  std::this_thread::sleep_for(std::chrono::milliseconds(120));
              }
          }) {}

    ~Spinner() { Stop(); }

    // Idempotent: the first call joins the background thread (so its
    // in-flight write to stderr is guaranteed complete, not racing
    // whatever the caller prints next) and erases the spinner's line;
    // later calls are a no-op. Callers stop the spinner as soon as
    // real content is about to be printed on the same line, so this
    // must fully settle before returning -- otherwise the spinner
    // thread's next frame can interleave with that content on the
    // shared console and corrupt it.
    void Stop() {
        if (waiting_.exchange(false, std::memory_order_relaxed)) {
            thread_.join();
            std::cerr << "\r" << std::string(28, ' ') << "\r" << std::flush;
        }
    }

private:
    std::atomic<bool> waiting_{true};
    std::thread thread_;
};

}  // namespace

std::string ChatCli::StreamChat(const std::string& messagesJson) const {
    json out;
    out["model"] = config_.model;
    out["messages"] = json::parse(messagesJson);
    out["stream"] = true;
    const std::string body = out.dump();

    static constexpr std::array<std::chrono::seconds, 3> kRetryDelays = {
        std::chrono::seconds(2), std::chrono::seconds(5), std::chrono::seconds(10)};

    for (size_t attempt = 0;; ++attempt) {
        Spinner spinner("waiting for Ollama");

        httplib::Client ollama(config_.ollamaHost, config_.ollamaPort);
        ollama.set_read_timeout(120, 0);
        ollama.set_write_timeout(600, 0);
        ollama.set_connection_timeout(10, 0);

        httplib::Request req;
        req.method = "POST";
        req.path = "/api/chat";
        req.set_header("Content-Type", "application/json");
        req.body = body;

        bool errored = false;
        bool sawContent = false;
        int httpStatus = 0;
        std::string errorMessage;
        std::string ndjsonBuffer;
        std::string reply;

        auto tryParseLine = [&](const std::string& line) {
            if (line.empty()) return;
            try {
                json chunk = json::parse(line);
                if (chunk.contains("error")) {
                    errorMessage = chunk.value("error", std::string{});
                    errored = true;
                    return;
                }
                std::string content =
                    chunk.value("message", json::object()).value("content", std::string{});
                if (!content.empty()) {
                    std::cout << content;
                    std::cout.flush();
                    reply += content;
                    sawContent = true;
                }
            } catch (const json::exception&) {
                // Partial chunk that doesn't parse on its own -- Ollama
                // always terminates NDJSON lines with '\n', so this only
                // happens if a line spans two content_receiver calls,
                // which the buffer above already re-joins on the next
                // iteration (or, for a single-shot error body with no
                // trailing '\n' at all, on the final flush below).
            }
        };

        // Always continue past a non-2xx response rather than aborting:
        // Ollama's error responses carry a JSON {"error": "..."} body
        // (e.g. "cudaMalloc failed: out of memory") that's only visible
        // by letting content_receiver read it below.
        req.response_handler = [&httpStatus](const httplib::Response& r) {
            httpStatus = r.status;
            return true;
        };
        req.content_receiver = [&](const char* data, size_t len, uint64_t, uint64_t) -> bool {
            spinner.Stop();
            ndjsonBuffer.append(data, len);
            std::size_t pos;
            while ((pos = ndjsonBuffer.find('\n')) != std::string::npos) {
                std::string line = ndjsonBuffer.substr(0, pos);
                ndjsonBuffer.erase(0, pos + 1);
                tryParseLine(line);
            }
            return true;
        };

        auto res = ollama.send(req);
        spinner.Stop();

        // Ollama's error body for a hard failure (e.g. model load OOM) is
        // often a single JSON object with no trailing newline, which the
        // line-splitting loop above never reaches -- flush whatever's
        // left in the buffer through the same parser.
        if (!ndjsonBuffer.empty()) {
            tryParseLine(ndjsonBuffer);
            ndjsonBuffer.clear();
        }

        if (!res) {
            if (errorMessage.empty()) {
                errorMessage = "Ollama request failed: " + httplib::to_string(res.error());
            }
            errored = true;
        } else if (httpStatus < 200 || httpStatus >= 300) {
            if (errorMessage.empty()) {
                errorMessage = "Ollama returned HTTP " + std::to_string(httpStatus);
            }
            errored = true;
        } else if (!sawContent) {
            // A 2xx with zero content chunks is not a clean success --
            // silently returning nothing here is worse than a visible
            // error, since the user has no idea their turn was dropped.
            errorMessage = "Ollama returned no content for this turn";
            errored = true;
        }

        if (!errored) {
            std::cout << "\n";
            return reply;
        }

        const bool retryable = !sawContent && IsRetryableStatus(httpStatus) && attempt < kRetryDelays.size();
        if (!retryable) {
            std::cerr << "\n" << rang::fg::red << rang::style::bold << "[error] " << errorMessage
                      << rang::style::reset << "\n";
            return {};
        }

        auto delay = kRetryDelays[attempt];
        std::cerr << rang::fg::yellow << "[retry] " << errorMessage << " -- retrying in "
                   << delay.count() << "s..." << rang::style::reset << "\n";
        std::this_thread::sleep_for(delay);
    }
}

int ChatCli::Run() {
    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::filesystem::path fileRoot = ResolveChatFileRoot(config_.fileRoot, cwd);
    std::error_code rootEc;
    if (!config_.fileRoot.empty()) {
        spdlog::info("ChatCli: /ls, /read and /write are confined to '{}'", fileRoot.string());
    } else if (!std::filesystem::exists(fileRoot / ".git", rootEc)) {
        spdlog::warn(
            "ChatCli: no enclosing git repository found -- /ls, /read and /write are confined "
            "to the current directory '{}'",
            cwd.string());
    }

    std::cout << rang::style::dim << "cppcoder chat -- model '" << rang::style::reset
              << rang::style::bold << config_.model << rang::style::reset << rang::style::dim
              << "' via " << config_.ollamaHost << ":" << config_.ollamaPort
              << ". Type /exit to quit." << rang::style::reset << "\n";

    json messages = json::array();
    std::string line;
    while (true) {
        std::cout << "\n" << rang::fg::cyan << rang::style::bold << "> " << rang::style::reset;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }
        if (line.empty()) continue;
        if (line == "/exit" || line == "/quit") break;

        LocalCommandResult localResult = TryHandleLocalCommand(line, fileRoot);
        if (localResult.handled) {
            std::cout << rang::style::dim << localResult.text << rang::style::reset << "\n";
            continue;
        }

        for (const auto& fact : ExtractFacts(line)) {
            memory_.AddFact(fact);
        }

        messages.push_back({{"role", "user"}, {"content", line}});

        json turnMessages = messages;
        if (config_.fileContextEnabled) {
            std::string fileContext =
                RunRetrievalPrePass(line, config_.model, config_.ollamaHost, config_.ollamaPort,
                                     fileRoot);
            if (!fileContext.empty()) {
                turnMessages.insert(turnMessages.begin(),
                                     json{{"role", "system"}, {"content", fileContext}});
            }
        }

        auto facts = memory_.AllFacts();
        if (!facts.empty()) {
            std::string systemContent =
                "Known facts about the user, remembered from earlier conversations:\n";
            for (const auto& fact : facts) {
                systemContent += "- " + fact + "\n";
            }
            turnMessages.insert(turnMessages.begin(),
                                 json{{"role", "system"}, {"content", systemContent}});
        }

        std::string reply = StreamChat(turnMessages.dump());
        if (!reply.empty()) {
            messages.push_back({{"role", "assistant"}, {"content", reply}});
        }
    }

    return 0;
}

}  // namespace cppcoder
