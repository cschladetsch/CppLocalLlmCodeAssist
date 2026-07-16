cmake_minimum_required(VERSION 3.24)
project(CppCoder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# This project is consumed as a git submodule or built standalone; it is
# never `cmake --install`'d. Without this, CMake's Generate step fails
# whenever a dependency ends up being an in-tree FetchContent target
# rather than a real installed package (e.g. nlohmann_json auto-fetched
# below) -- external/CppLmmModelStore unconditionally runs install(EXPORT)
# and CMake validates that at generate time regardless of whether install
# will ever actually run.
set(CMAKE_SKIP_INSTALL_RULES ON)

include(FetchContent)

# Prefer an already-installed nlohmann_json (apt install
# nlohmann-json3-dev, vcpkg, conan, ...); otherwise fetch it ourselves.
# OVERRIDE_FIND_PACKAGE (CMake >= 3.24) makes every later find_package(nlohmann_json)
# call in this build -- including the one inside external/CppLmmModelStore --
# transparently resolve to this fetched copy instead of re-fetching or
# failing, so no system install is required on any platform.
#
# UPDATE_DISCONNECTED TRUE matters here: without it, FetchContent re-runs
# the git update step (a network round-trip to check for new commits on
# the tag) on *every* configure, not just the first. With a pinned tag
# that can never usefully change, that's pure overhead -- so we fetch
# once into build/_deps and never touch the network again unless build/
# is deleted (-Clean).
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    if(EXISTS "${CMAKE_BINARY_DIR}/_deps/nlohmann_json-src")
        message(STATUS "nlohmann_json: not installed system-wide, reusing cached fetch in build/_deps")
    else()
        message(STATUS "nlohmann_json: not installed system-wide, fetching once into build/_deps")
    endif()
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
        UPDATE_DISCONNECTED TRUE
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.1
    GIT_SHALLOW TRUE
    UPDATE_DISCONNECTED TRUE
)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(httplib)

find_package(Threads REQUIRED)

option(BUILD_EXAMPLES "Build examples/ (replay_demo, minimal_usage)" ON)
option(BUILD_TESTS "Build tests" ON)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/spdlog)

# googletest is added here -- once, before CppLmmModelStore -- so both
# CppCoder's own tests/ and CppLmmModelStore's own test suite share the
# same GTest::gtest_main target instead of each add_subdirectory-ing it
# separately (gtest defines global target names, so adding it twice in
# one CMake project fails).
if(BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(external/googletest)
endif()

# nlohmann_json and spdlog are resolved above (system copy or the
# OVERRIDE_FIND_PACKAGE fetch; this project's own add_subdirectory), and
# googletest too when tests are enabled -- so ModelStore's own
# find_package()/TARGET checks for all three are satisfied and it
# doesn't need to fetch or vendor a second, export-incompatible copy of
# any of them.
set(MODELSTORE_BUILD_TESTS ${BUILD_TESTS} CACHE BOOL "" FORCE)
set(MODELSTORE_ALLOW_FETCHCONTENT OFF CACHE BOOL "" FORCE)
add_subdirectory(external/CppLmmModelStore)

add_subdirectory(src)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
