#include "cppcoder/ChatCli.h"

#include "cppcoder/FactExtractor.h"
#include "cppcoder/FileRetriever.h"
#include "cppcoder/LocalCommands.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace cppcoder {

using json = nlohmann::json;

ChatCli::ChatCli(ChatCliConfig config)
    : config_(std::move(config)), memory_(config_.memoryFilePath) {}

std::string ChatCli::StreamChat(const std::string& messagesJson) const {
    json out;
    out["model"] = config_.model;
    out["messages"] = json::parse(messagesJson);
    out["stream"] = true;

    httplib::Client ollama(config_.ollamaHost, config_.ollamaPort);
    ollama.set_read_timeout(120, 0);
    ollama.set_write_timeout(600, 0);
    ollama.set_connection_timeout(10, 0);

    httplib::Request req;
    req.method = "POST";
    req.path = "/api/chat";
    req.set_header("Content-Type", "application/json");
    req.body = out.dump();

    bool errored = false;
    std::string ndjsonBuffer;
    std::string reply;

    req.response_handler = [&errored](const httplib::Response& r) {
        if (r.status >= 200 && r.status < 300) return true;
        std::cerr << "\n[error] Ollama returned HTTP " << r.status << "\n";
        errored = true;
        return false;
    };
    req.content_receiver = [&](const char* data, size_t len, uint64_t, uint64_t) -> bool {
        ndjsonBuffer.append(data, len);
        std::size_t pos;
        while ((pos = ndjsonBuffer.find('\n')) != std::string::npos) {
            std::string line = ndjsonBuffer.substr(0, pos);
            ndjsonBuffer.erase(0, pos + 1);
            if (line.empty()) continue;
            try {
                json chunk = json::parse(line);
                if (chunk.contains("error")) {
                    std::cerr << "\n[error] " << chunk.value("error", std::string{}) << "\n";
                    errored = true;
                    continue;
                }
                std::string content = chunk.value("message", json::object()).value("content", std::string{});
                if (!content.empty()) {
                    std::cout << content;
                    std::cout.flush();
                    reply += content;
                }
            } catch (const json::exception&) {
                // Partial chunk that doesn't parse on its own -- Ollama
                // always terminates NDJSON lines with '\n', so this only
                // happens if a line spans two content_receiver calls,
                // which the buffer above already re-joins on the next
                // iteration.
            }
        }
        return true;
    };

    auto res = ollama.send(req);
    if (!res && !errored) {
        std::cerr << "\n[error] Ollama request failed: " << httplib::to_string(res.error()) << "\n";
        errored = true;
    }
    std::cout << "\n";

    return errored ? std::string{} : reply;
}

int ChatCli::Run() {
    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::filesystem::path fileRoot = FindRepoRoot(cwd);
    std::error_code rootEc;
    if (!std::filesystem::exists(fileRoot / ".git", rootEc)) {
        spdlog::warn(
            "ChatCli: no enclosing git repository found -- /ls, /read and /write are confined "
            "to the current directory '{}'",
            cwd.string());
    }

    std::cout << "cppcoder chat -- model '" << config_.model << "' via " << config_.ollamaHost
              << ":" << config_.ollamaPort << ". Type /exit to quit.\n";

    json messages = json::array();
    std::string line;
    while (true) {
        std::cout << "\n> ";
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }
        if (line.empty()) continue;
        if (line == "/exit" || line == "/quit") break;

        LocalCommandResult localResult = TryHandleLocalCommand(line, fileRoot);
        if (localResult.handled) {
            std::cout << localResult.text << "\n";
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
