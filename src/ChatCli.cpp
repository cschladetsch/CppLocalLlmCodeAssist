#include "cppcoder/ChatCli.h"
#include <iostream>
#include <string>
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace cppcoder {

ChatCli::ChatCli(ChatCliConfig config) : config_(config) {}

void ChatCli::Run() {
    std::cout << "========================================\n";
    std::cout << " CppCoder Interactive CLI (Ollama)\n";
    std::cout << " Type 'exit' or 'quit' to leave.\n";
    std::cout << "========================================\n";

    httplib::Client ollama("http://localhost:11434");
    ollama.set_read_timeout(config_.timeoutSeconds, 0);

    std::string input;
    while (true) {
        std::cout << "\n> ";
        if (!std::getline(std::cin, input)) { break; }
        if (input.empty()) { continue; }
        if (input == "exit" || input == "quit") {
            std::cout << "Exiting ChatCli.\n";
            break;
        }

        nlohmann::json body = {
            {"model", "codellama"},
            {"prompt", input},
            {"stream", false}
        };

        auto res = ollama.Post("/api/generate", body.dump(), "application/json");
        if (res && res->status == 200) {
            try {
                auto jsonRes = nlohmann::json::parse(res->body);
                if (jsonRes.contains("response")) {
                    std::cout << "\n" << jsonRes["response"].get<std::string>() << "\n";
                } else {
                    std::cout << "\n[Response]: " << res->body << "\n";
                }
            } catch (...) {
                std::cout << "\n[Response Raw]: " << res->body << "\n";
            }
        } else {
            std::cout << "[Error] Failed to connect to Ollama (status: " 
                      << (res ? std::to_string(res->status) : "connection failed") << ")\n";
        }
    }
}

} // namespace cppcoder
