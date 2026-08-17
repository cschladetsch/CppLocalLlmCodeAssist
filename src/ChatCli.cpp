#include "cppcoder/ChatCli.h"
#include <iostream>
#include <fstream>
#include <string>
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace cppcoder {

ChatCli::ChatCli(ChatCliConfig config) : config_(config) {}

void ChatCli::Run() {
    std::cout << "========================================\n";
    std::cout << " Sarah -- Sassy C++ Assistant (w/ Memory)\n";
    std::cout << " Type 'exit' or 'quit' to leave.\n";
    std::cout << "========================================\n";

    std::string modelName = "codellama";
    try {
        std::ifstream configFile("config.json");
        if (configFile.is_open()) {
            nlohmann::json j;
            configFile >> j;
            if (j.contains("model")) {
                modelName = j["model"].get<std::string>();
            }
        }
    } catch (...) {}

    std::cout << "[Config] Active Model: " << modelName << "\n\n";

    httplib::Client ollama("http://localhost:11434");
    ollama.set_read_timeout(config_.timeoutSeconds, 0);

    // Initialize conversation history with the system persona
    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({
        {"role", "system"},
        {"content", "You are Sarah, a flirtatious, playfully arrogant, and sharp-witted senior engineer. You tease the user with a seductive, confident, and mocking edge, never taking them entirely seriously while still delivering the technical answer. You remember everything discussed in this session."}
    });

    std::string input;
    while (true) {
        std::cout << "\n> ";
        if (!std::getline(std::cin, input)) { break; }
        if (input.empty()) { continue; }
        if (input == "exit" || input == "quit") {
            std::cout << "Sarah smirks and walks away.\n";
            break;
        }

        // Append user prompt to session history
        messages.push_back({
            {"role", "user"},
            {"content", input}
        });

        nlohmann::json body = {
            {"model", modelName},
            {"messages", messages},
            {"stream", false}
        };

        auto res = ollama.Post("/api/chat", body.dump(), "application/json");
        if (res && res->status == 200) {
            try {
                auto jsonRes = nlohmann::json::parse(res->body);
                if (jsonRes.contains("message") && jsonRes["message"].contains("content")) {
                    std::string reply = jsonRes["message"]["content"].get<std::string>();
                    std::cout << "\n" << reply << "\n";

                    // Append assistant reply to history to maintain session memory
                    messages.push_back({
                        {"role", "assistant"},
                        {"content", reply}
                    });
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
