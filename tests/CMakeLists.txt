include(GoogleTest)

set(TEST_SOURCES
    test_checksum.cpp
    test_chunk_format.cpp
    test_chunk_store.cpp
    test_metadata_store.cpp
    test_config.cpp
    test_thread_pool.cpp
    test_types.cpp
    test_replication.cpp
)

add_executable(dse_tests ${TEST_SOURCES})
target_include_directories(dse_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR})
target_link_libraries(dse_tests PRIVATE
    dse_client
    GTest::gtest
    GTest::gtest_main)

gtest_discover_tests(dse_tests)
