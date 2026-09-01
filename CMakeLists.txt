cmake_minimum_required(VERSION 3.20)
project(DistributedStorageEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DSE_BUILD_TESTS "Build unit tests" ON)
option(DSE_USE_ROCKSDB "Enable RocksDB metadata backend" OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
endif()
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Dependencies
find_package(Protobuf REQUIRED)
find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(GRPC grpc++)
endif()

if(GRPC_FOUND)
    set(GRPC_LIBS ${GRPC_LIBRARIES})
    set(GRPC_INCLUDE_DIRS ${GRPC_INCLUDE_DIRS})
    find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
else()
    find_package(gRPC CONFIG REQUIRED)
    set(GRPC_LIBS gRPC::grpc++ gRPC::grpc++_reflection)
    set(GRPC_CPP_PLUGIN $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
endif()

find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
    pkg_check_modules(SPDLOG REQUIRED spdlog)
endif()

find_package(Threads REQUIRED)

if(DSE_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        find_library(GTEST_LIB gtest REQUIRED)
        find_library(GTEST_MAIN_LIB gtest_main REQUIRED)
        find_path(GTEST_INCLUDE_DIR gtest/gtest.h REQUIRED)
        add_library(GTest::gtest UNKNOWN IMPORTED)
        set_target_properties(GTest::gtest PROPERTIES
            IMPORTED_LOCATION "${GTEST_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}")
        add_library(GTest::gtest_main UNKNOWN IMPORTED)
        set_target_properties(GTest::gtest_main PROPERTIES
            IMPORTED_LOCATION "${GTEST_MAIN_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}")
    endif()
endif()

if(DSE_USE_ROCKSDB)
    find_package(RocksDB CONFIG REQUIRED)
    add_compile_definitions(DSE_USE_ROCKSDB)
endif()

# Proto generation
set(PROTO_DIR ${CMAKE_SOURCE_DIR}/proto)
set(PROTO_FILES
    ${PROTO_DIR}/storage.proto
    ${PROTO_DIR}/metadata.proto
    ${PROTO_DIR}/coordinator.proto
)

set(GENERATED_PROTO_SRCS "")
set(GENERATED_PROTO_HDRS "")

foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    list(APPEND GENERATED_PROTO_SRCS
        "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.cc"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.cc")
    list(APPEND GENERATED_PROTO_HDRS
        "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.h"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.h")
endforeach()

add_custom_command(
    OUTPUT ${GENERATED_PROTO_SRCS} ${GENERATED_PROTO_HDRS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
         --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
         --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
         -I${PROTO_DIR}
         storage.proto metadata.proto coordinator.proto
    WORKING_DIRECTORY ${PROTO_DIR}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating protobuf sources"
)

add_library(dse_proto STATIC ${GENERATED_PROTO_SRCS})
target_include_directories(dse_proto PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${GRPC_INCLUDE_DIRS})
target_link_libraries(dse_proto PUBLIC
    protobuf::libprotobuf
    ${GRPC_LIBS})

# Common library
add_library(dse_common STATIC
    src/common/config.cpp
    src/common/checksum.cpp
    src/common/thread_pool.cpp
    src/chunk/chunk_format.cpp
    src/chunk/chunk_store.cpp
    src/network/grpc_util.cpp
)
target_include_directories(dse_common PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(dse_common PUBLIC
    dse_proto
    Threads::Threads)
if(TARGET spdlog::spdlog)
    target_link_libraries(dse_common PUBLIC spdlog::spdlog)
else()
    target_include_directories(dse_common PUBLIC ${SPDLOG_INCLUDE_DIRS})
    target_link_libraries(dse_common PUBLIC ${SPDLOG_LIBRARIES})
endif()

# Metadata library
add_library(dse_metadata STATIC
    src/metadata/metadata_store.cpp
    src/metadata/metadata_service_impl.cpp
)
target_link_libraries(dse_metadata PUBLIC dse_common)

# Storage library
add_library(dse_storage STATIC
    src/storage/storage_service_impl.cpp
)
target_link_libraries(dse_storage PUBLIC dse_common)

# Coordinator & replication
add_library(dse_coordination STATIC
    src/coordinator/coordinator_service.cpp
    src/replication/replication_manager.cpp
    src/recovery/recovery_manager.cpp
    src/heartbeat/heartbeat_manager.cpp
)
target_link_libraries(dse_coordination PUBLIC dse_metadata dse_storage)

# Client library
add_library(dse_client STATIC
    src/client/storage_client.cpp
)
target_link_libraries(dse_client PUBLIC dse_coordination)

# Executables
add_executable(dse-storage-node src/storage/storage_node_main.cpp)
target_link_libraries(dse-storage-node PRIVATE dse_storage dse_coordination)

add_executable(dse-metadata-service src/metadata/metadata_node_main.cpp)
target_link_libraries(dse-metadata-service PRIVATE dse_coordination)

add_executable(storage src/client/cli_main.cpp)
target_link_libraries(storage PRIVATE dse_client)

# Tests
if(DSE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Install
install(TARGETS dse-storage-node dse-metadata-service storage
        RUNTIME DESTINATION bin)
