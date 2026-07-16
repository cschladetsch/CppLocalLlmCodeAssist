add_library(cppcoder_core
    JsonUtil.cpp
    Logging.cpp
    OllamaClient.cpp
    CodebaseScanner.cpp
    Worker.cpp
    Judge.cpp
    TaskQueue.cpp
    ResearchEngine.cpp
    ChatServer.cpp
)
target_include_directories(cppcoder_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(cppcoder_core
    PUBLIC
        nlohmann_json::nlohmann_json
        ModelStore::ModelStore
        spdlog::spdlog
    PRIVATE
        httplib::httplib
        Threads::Threads
)

add_executable(cppcoder main.cpp)
target_link_libraries(cppcoder PRIVATE cppcoder_core)
