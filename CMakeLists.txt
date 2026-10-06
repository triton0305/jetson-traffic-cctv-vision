cmake_minimum_required(VERSION 3.16)

project(edge_vision
    VERSION 1.0.0
    LANGUAGES CXX
)

# =============================================================================
# C++ Settings
# =============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =============================================================================
# Build Type
# =============================================================================

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# =============================================================================
# Output Directory
# =============================================================================

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# =============================================================================
# Dependencies
# =============================================================================

find_package(OpenCV REQUIRED COMPONENTS
    core
    imgproc
    highgui
    videoio
    dnn
)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

add_library(cctv_provider src/input/utic_cctv_provider.cpp)
target_include_directories(cctv_provider PUBLIC include)
target_link_libraries(cctv_provider PUBLIC CURL::libcurl)
target_compile_options(cctv_provider PRIVATE -Wall -Wextra -Wpedantic)

add_library(cctv_stream src/input/cctv_stream.cpp)
target_include_directories(cctv_stream PUBLIC include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(cctv_stream PUBLIC ${OpenCV_LIBS} Threads::Threads)
target_compile_options(cctv_stream PRIVATE -Wall -Wextra -Wpedantic)

# =============================================================================
# Paths
# =============================================================================

if(NOT DEFINED MODEL_PATH)
  set(MODEL_PATH "${CMAKE_SOURCE_DIR}/models/yolo26n_fp16.engine")
endif()

if(NOT DEFINED BOOT_ID_PATH)
  set(BOOT_ID_PATH "${CMAKE_SOURCE_DIR}/boot_id.dat")
endif()

# =============================================================================
# Sources
# =============================================================================

set(SOURCES
    src/main.cpp
    src/core/boot_id.cpp
    src/core/message_id.cpp
    src/core/runtime_state.cpp
    src/core/metrics.cpp
    src/vision/vision_worker.cpp
    src/vision/tracker.cpp
    src/vision/preprocessor.cpp
    src/vision/detector.cpp
    src/vision/postprocessor.cpp
    src/protocol/serializer.cpp
    src/protocol/snapshot_publisher.cpp
    src/network/tcp_client.cpp
    src/network/message_queue.cpp
    src/network/network_worker.cpp
)

# =============================================================================
# Executable
# =============================================================================

add_executable(edge_vision
    ${SOURCES}
)

# =============================================================================
# Include Directories
# =============================================================================

target_include_directories(edge_vision
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
        /usr/include/aarch64-linux-gnu
        /usr/local/cuda/include
)

# =============================================================================
# Libraries
# =============================================================================

target_link_libraries(edge_vision
    PRIVATE
        ${OpenCV_LIBS}
        Threads::Threads
        cctv_provider
        cctv_stream
        nvinfer
        cudart
)

# =============================================================================
# Compile Definitions
# =============================================================================

target_compile_definitions(edge_vision
    PRIVATE
        MODEL_PATH="${MODEL_PATH}"
        BOOT_ID_FILE_PATH="${BOOT_ID_PATH}"
)

# =============================================================================
# Compiler Warnings
# =============================================================================

target_compile_options(edge_vision
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# =============================================================================
# Release Optimization
# =============================================================================

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(edge_vision
        PRIVATE
            -O3
    )
endif()

# =============================================================================
# Sanitizer
# Usage:
# cmake -B build -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
# =============================================================================

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(ENABLE_SANITIZERS)
    target_compile_options(edge_vision
        PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
    )

    target_link_options(edge_vision
        PRIVATE
            -fsanitize=address,undefined
    )
endif()

install(TARGETS edge_vision RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
  add_executable(snapshot_test tests/snapshot_test.cpp
    src/protocol/snapshot_publisher.cpp src/protocol/serializer.cpp
    src/core/message_id.cpp src/core/runtime_state.cpp src/core/metrics.cpp
    src/network/message_queue.cpp)
  target_include_directories(snapshot_test PRIVATE include)
  target_compile_definitions(snapshot_test PRIVATE BOOT_ID_FILE_PATH="unused")
  target_link_libraries(snapshot_test PRIVATE Threads::Threads)
  target_compile_options(snapshot_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME snapshot COMMAND snapshot_test)

  add_executable(utic_cctv_provider_test tests/utic_cctv_provider_test.cpp)
  target_link_libraries(utic_cctv_provider_test PRIVATE cctv_provider)
  add_test(NAME utic_cctv_provider COMMAND utic_cctv_provider_test)
endif()

if(BUILD_TESTING)
  add_executable(network_integration_test
    tests/network_integration.cpp
    src/network/tcp_client.cpp
    src/network/message_queue.cpp
    src/network/network_worker.cpp
    src/core/runtime_state.cpp
    src/core/metrics.cpp
    src/core/message_id.cpp
    src/protocol/serializer.cpp)
  target_include_directories(network_integration_test PRIVATE include)
  target_compile_definitions(network_integration_test PRIVATE BOOT_ID_FILE_PATH="unused")
  target_compile_options(network_integration_test PRIVATE -Wall -Wextra -Wpedantic)
  target_link_libraries(network_integration_test PRIVATE Threads::Threads)
  add_test(NAME network_integration COMMAND network_integration_test)
  set_tests_properties(network_integration PROPERTIES TIMEOUT 30)
endif()

if(BUILD_TESTING)
  add_executable(cctv_recovery_test tests/cctv_recovery_test.cpp)
  target_link_libraries(cctv_recovery_test PRIVATE cctv_stream)
  target_link_options(cctv_recovery_test PRIVATE -Wl,--wrap=nanosleep)
  add_test(NAME cctv_recovery COMMAND cctv_recovery_test)
  set_tests_properties(cctv_recovery PROPERTIES TIMEOUT 10)
endif()
