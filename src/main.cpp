#include "cppcoder/ChatServer.h"
#include "cppcoder/CodebaseScanner.h"
#include "cppcoder/Logging.h"
#include "cppcoder/OllamaClient.h"
#include "cppcoder/ResearchEngine.h"

#include <ModelStore.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void PrintUsage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " --question \"...\" --codebase <path> [options]\n"
        << "   or: " << argv0 << " --serve [options]\n"
        << "\n"
        << "Research mode options:\n"
        << "  --question <text>        Question to research (required unless --serve)\n"
        << "  --codebase <path>        Root of the codebase to investigate (required unless --serve)\n"
        << "  --max-minutes <n>        Wall clock budget in minutes (default: 90)\n"
        << "  --max-iterations <n>     Max task loop iterations (default: 200)\n"
        << "  --token-budget <n>       Approx tokens per task (default: 120000)\n"
        << "  --events-file <path>     Write JSON-lines engine events for the web UI\n"
        << "\n"
        << "Chat server mode options:\n"
        << "  --serve                  Start the web chat UI + API instead of researching\n"
        << "  --serve-host <addr>      Address to bind the chat server to (default: 127.0.0.1)\n"
        << "  --serve-port <port>      Port to bind the chat server to (default: 8765)\n"
        << "  --web-root <path>        Directory to serve as the chat UI (default: auto-detect ./web)\n"
        << "\n"
        << "Shared Ollama options:\n"
        << "  --model <name>           Ollama model tag (default: qwen2.5-coder:7b)\n"
        << "  --host <host>            Ollama host (default: localhost)\n"
        << "  --port <port>            Ollama port (default: 11434)\n"
        << "\n"
        << "Logging options:\n"
        << "  --log-level <level>      trace|debug|info|warn|err|critical|off (default: info)\n"
        << "  --log-file <path>        Also write logs to this file\n";
}

// Best-effort discovery of the web/ directory (containing chat.html) so
// `--serve` works out of the box whether run from the repo root or via
// the built binary in build/src/. Falls back to an empty string (static
// UI won't be served, but the JSON API still works) if nothing is found.
std::string ResolveDefaultWebRoot(const char* argv0) {
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::path cwdCandidate = fs::absolute(fs::path("web"), ec);
    if (!ec && fs::exists(cwdCandidate / "chat.html")) {
        return cwdCandidate.string();
    }

    fs::path exePath = fs::absolute(fs::path(argv0), ec);
    if (!ec) {
        // Typical build layout: <repo>/build/src/cppcoder(.exe) -> <repo>/web
        fs::path candidate = exePath.parent_path() / ".." / ".." / "web";
        candidate = fs::weakly_canonical(candidate, ec);
        if (!ec && fs::exists(candidate / "chat.html")) {
            return candidate.string();
        }
    }

    return {};
}

}  // namespace

int main(int argc, char** argv) {
    std::string question;
    std::string codebasePath;
    std::string eventsFilePath;
    std::string logLevel = "info";
    std::string logFilePath;
    cppcoder::OllamaConfig ollamaConfig;
    cppcoder::EngineConfig engineConfig;

    bool serveMode = false;
    std::string serveHost = "127.0.0.1";
    int servePort = 8765;
    std::string webRoot;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << "\n";
                std::exit(1);
            }
            return argv[++i];
        };

        if (arg == "--question") {
            question = next("--question");
        } else if (arg == "--codebase") {
            codebasePath = next("--codebase");
        } else if (arg == "--model") {
            ollamaConfig.model = next("--model");
        } else if (arg == "--host") {
            ollamaConfig.host = next("--host");
        } else if (arg == "--port") {
            ollamaConfig.port = std::stoi(next("--port"));
        } else if (arg == "--max-minutes") {
            engineConfig.maxWallClock = std::chrono::minutes(std::stoi(next("--max-minutes")));
        } else if (arg == "--max-iterations") {
            engineConfig.maxIterations = std::stoi(next("--max-iterations"));
        } else if (arg == "--token-budget") {
            engineConfig.tokenBudgetPerTask = std::stoull(next("--token-budget"));
        } else if (arg == "--events-file") {
            eventsFilePath = next("--events-file");
        } else if (arg == "--serve") {
            serveMode = true;
        } else if (arg == "--serve-host") {
            serveHost = next("--serve-host");
        } else if (arg == "--serve-port") {
            servePort = std::stoi(next("--serve-port"));
        } else if (arg == "--web-root") {
            webRoot = next("--web-root");
        } else if (arg == "--log-level") {
            logLevel = next("--log-level");
        } else if (arg == "--log-file") {
            logFilePath = next("--log-file");
        } else if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    cppcoder::InitLogging(logLevel, logFilePath);

    if (serveMode) {
        cppcoder::ChatServerConfig serverConfig;
        serverConfig.bindHost = serveHost;
        serverConfig.bindPort = servePort;
        serverConfig.ollamaHost = ollamaConfig.host;
        serverConfig.ollamaPort = ollamaConfig.port;
        serverConfig.defaultModel = ollamaConfig.model;
        serverConfig.webRoot = webRoot.empty() ? ResolveDefaultWebRoot(argv[0]) : webRoot;

        if (serverConfig.webRoot.empty()) {
            spdlog::warn(
                "Could not locate a web/ directory with chat.html; static UI won't be served "
                "(pass --web-root explicitly). The /api/* endpoints still work.");
        }

        cppcoder::ChatServer server(std::move(serverConfig));
        return server.Run();
    }

    if (question.empty() || codebasePath.empty()) {
        PrintUsage(argv[0]);
        return 1;
    }

    // Zero-model-duplication convention: verify the requested model
    // resolves in the shared model store before spending any research
    // time on it. Ollama manages its own runtime, so this is advisory
    // only -- it does not block the run.
    if (!deepseek::ModelStore::ModelExists(ollamaConfig.model)) {
        spdlog::info(
            "'{}' not found under shared model store ({}). Continuing -- Ollama manages its "
            "own model storage separately.",
            ollamaConfig.model, deepseek::ModelStore::ResolveModelPath(ollamaConfig.model));
    }

    cppcoder::OllamaClient client(ollamaConfig);
    if (!client.IsModelAvailable()) {
        spdlog::warn(
            "Ollama at {}:{} does not report model '{}' as available. Run `ollama pull {}` "
            "first. Continuing anyway.",
            ollamaConfig.host, ollamaConfig.port, ollamaConfig.model, ollamaConfig.model);
    }

    cppcoder::CodebaseScanner scanner(codebasePath);
    cppcoder::ResearchEngine engine(std::move(client), std::move(scanner), engineConfig);

    std::ofstream eventsFile;
    if (!eventsFilePath.empty()) {
        eventsFile.open(eventsFilePath, std::ios::out | std::ios::trunc);
        if (!eventsFile) {
            spdlog::warn("Could not open events file '{}' for writing; continuing without "
                         "event output.",
                         eventsFilePath);
        } else {
            engine.SetEventSink([&eventsFile](const std::string& line) {
                eventsFile << line << "\n";
                eventsFile.flush();
            });
        }
    }

    spdlog::info("Researching: {}", question);
    cppcoder::ResearchResult result = engine.Research(question);

    std::cout << "\n=== Research complete ===\n"
              << "Answered: " << (result.answered ? "yes" : "no") << "\n"
              << "Termination: " << result.terminationReason << "\n"
              << "Iterations: " << result.iterationsRun << "\n"
              << "Wall clock: " << result.wallClock.count() << " ms\n"
              << "Successful findings: " << result.successfulFindings.size() << "\n\n";

    if (result.answered) {
        std::cout << "Answer:\n" << result.answer << "\n";
    } else {
        std::cout << "No answer produced. Findings gathered along the way:\n";
        for (const auto& f : result.successfulFindings) {
            std::cout << "- [" << f.areaInvestigated << "] " << f.summary << "\n";
        }
    }

    return result.answered ? 0 : 2;
}
