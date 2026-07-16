cmake_minimum_required(VERSION 3.20)
project(ModelStore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

option(MODELSTORE_ALLOW_FETCHCONTENT "Allow downloading dependencies via FetchContent" OFF)

find_package(nlohmann_json CONFIG QUIET)
if (NOT nlohmann_json_FOUND)
  if (MODELSTORE_ALLOW_FETCHCONTENT)
    include(FetchContent)
    FetchContent_Declare(
      nlohmann_json
      URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
  else()
    message(FATAL_ERROR "nlohmann_json not found. Install nlohmann-json3-dev or enable MODELSTORE_ALLOW_FETCHCONTENT.")
  endif()
endif()

# spdlog backs all logging in this library (model-home resolution, path
# sanitization, directory creation, stream-parse errors). If a parent
# project already provides a spdlog::spdlog target -- e.g. CppCoder's own
# add_subdirectory(external/spdlog) run before this submodule is added --
# reuse it rather than vendoring a second copy. Otherwise fall back to a
# system install, a vendored checkout at ../spdlog or
# ../third_party/spdlog, or FetchContent.
if (NOT TARGET spdlog::spdlog)
  find_package(spdlog CONFIG QUIET)
  if (NOT spdlog_FOUND)
    set(SPDLOG_SOURCE_DIR "")
    foreach(candidate ${CMAKE_CURRENT_SOURCE_DIR}/../spdlog ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/spdlog)
      if (EXISTS ${candidate}/CMakeLists.txt)
        set(SPDLOG_SOURCE_DIR ${candidate})
        break()
      endif()
    endforeach()

    if (SPDLOG_SOURCE_DIR)
      set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
      set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
      add_subdirectory(${SPDLOG_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/spdlog)
    elseif (MODELSTORE_ALLOW_FETCHCONTENT)
      include(FetchContent)
      FetchContent_Declare(
        spdlog
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.tar.gz
      )
      FetchContent_MakeAvailable(spdlog)
    else()
      message(FATAL_ERROR "spdlog not found. Install it, vendor it at ../spdlog or "
                           "../third_party/spdlog, or enable MODELSTORE_ALLOW_FETCHCONTENT.")
    endif()
  endif()
endif()

add_library(ModelStore
  src/ModelStore.cpp
  src/DeepSeekStreamParser.cpp
)
add_library(ModelStore::ModelStore ALIAS ModelStore)
set_target_properties(ModelStore PROPERTIES
  VERSION 1.0.0
  SOVERSION 1
)
target_include_directories(ModelStore PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(ModelStore PUBLIC nlohmann_json::nlohmann_json spdlog::spdlog)
target_compile_features(ModelStore PUBLIC cxx_std_20)

option(MODELSTORE_BUILD_TESTS "Build ModelStore tests" ON)
if (MODELSTORE_BUILD_TESTS)
  set(MODELSTORE_TESTS_ENABLED ON)
  if (NOT TARGET GTest::gtest_main)
    set(GTEST_SOURCE_DIR "")
    foreach(candidate ${CMAKE_CURRENT_SOURCE_DIR}/../googletest ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/googletest)
      if (EXISTS ${candidate}/CMakeLists.txt)
        set(GTEST_SOURCE_DIR ${candidate})
        break()
      endif()
    endforeach()

    if (GTEST_SOURCE_DIR)
      set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
      set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
      set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
      add_subdirectory(${GTEST_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/googletest)
    else()
      if (NOT MODELSTORE_ALLOW_FETCHCONTENT)
        message(WARNING "ModelStore tests disabled: missing vendored googletest (looked in "
                         "../googletest and ../third_party/googletest) and "
                         "MODELSTORE_ALLOW_FETCHCONTENT=OFF.")
        set(MODELSTORE_TESTS_ENABLED OFF)
      else()
        include(FetchContent)
        FetchContent_Declare(
          googletest
          URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
      endif()
    endif()
  endif()

  if (MODELSTORE_TESTS_ENABLED)
    enable_testing()
    include(GoogleTest)

    add_executable(ModelStoreTests tests/ModelStoreTests.cpp)
    target_link_libraries(ModelStoreTests PRIVATE ModelStore GTest::gtest_main)
    gtest_discover_tests(ModelStoreTests)

    add_executable(StreamParserTests tests/StreamParserTests.cpp)
    target_link_libraries(StreamParserTests PRIVATE ModelStore GTest::gtest_main)
    gtest_discover_tests(StreamParserTests)
  endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(MODELSTORE_INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/ModelStore)

install(TARGETS ModelStore
  EXPORT ModelStoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
  include/ModelStore.hpp
  include/DeepSeekStreamParser.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT ModelStoreTargets
  NAMESPACE ModelStore::
  DESTINATION ${MODELSTORE_INSTALL_CONFIGDIR}
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ModelStoreConfigVersion.cmake
  VERSION 1.0.0
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  ${CMAKE_CURRENT_LIST_DIR}/cmake/ModelStoreConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ModelStoreConfig.cmake
  INSTALL_DESTINATION ${MODELSTORE_INSTALL_CONFIGDIR}
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/ModelStoreConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/ModelStoreConfigVersion.cmake
  DESTINATION ${MODELSTORE_INSTALL_CONFIGDIR}
)

set(DEEPSEEK_MODELS "deepseek-r1" CACHE STRING "Semicolon-separated model names to ensure")
set(ENSURE_MODELS_STAMP ${CMAKE_CURRENT_BINARY_DIR}/.ensure_models.stamp)
set(ENSURE_MODELS_LIST ${CMAKE_CURRENT_BINARY_DIR}/.ensure_models.list)

file(WRITE ${ENSURE_MODELS_LIST} "${DEEPSEEK_MODELS}")

add_custom_command(
  OUTPUT ${ENSURE_MODELS_STAMP}
  DEPENDS ${ENSURE_MODELS_LIST}
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/scripts/ensure_models.py
          --model ${DEEPSEEK_MODELS}
  COMMAND ${CMAKE_COMMAND} -E touch ${ENSURE_MODELS_STAMP}
  COMMENT "Ensuring DeepSeek models in the global model store"
  VERBATIM
)

option(MODELSTORE_AUTO_ENSURE_MODELS "Run ensure_models as part of the default build" OFF)
if (MODELSTORE_AUTO_ENSURE_MODELS)
  add_custom_target(ensure_models ALL
    DEPENDS ${ENSURE_MODELS_STAMP}
  )
else()
  add_custom_target(ensure_models
    DEPENDS ${ENSURE_MODELS_STAMP}
  )
endif()
