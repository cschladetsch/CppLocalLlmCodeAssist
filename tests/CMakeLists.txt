# googletest itself is added once at the top level (before
# external/CppLmmModelStore, so both test suites share one GTest target)
# -- see the top-level CMakeLists.txt for details.

add_executable(cppcoder_tests
    TypesTests.cpp
    JsonUtilTests.cpp
    TaskQueueTests.cpp
    CodebaseScannerTests.cpp
    WorkerTests.cpp
    JudgeTests.cpp
    ResearchEngineTests.cpp
)
target_link_libraries(cppcoder_tests PRIVATE cppcoder_core GTest::gtest_main)
target_include_directories(cppcoder_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

include(GoogleTest)
gtest_discover_tests(cppcoder_tests)
