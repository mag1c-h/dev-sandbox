cmake_minimum_required(VERSION 3.14)
project(dev-sandbox VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find CANN
if(DEFINED ENV{ASCEND_HOME_PATH})
    set(CANN_ROOT $ENV{ASCEND_HOME_PATH})
elseif(DEFINED ENV{ASCEND_CUSTOM_PATH})
    set(CANN_ROOT $ENV{ASCEND_CUSTOM_PATH}/latest)
else()
    set(CANN_ROOT /usr/local/Ascend/ascend-toolkit/latest)
endif()

message(STATUS "CANN_ROOT: ${CANN_ROOT}")

# Include directories
include_directories(SYSTEM ${CANN_ROOT}/include)
include_directories(SYSTEM ${CANN_ROOT}/include/experiment)
include_directories(SYSTEM ${CANN_ROOT}/include/experiment/runtime)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Link directories
link_directories(${CANN_ROOT}/lib64)

# Enable NPU (Ascend) FFTS support
add_compile_definitions(USE_NPU)

# Source files
set(COMMON_SRC
    src/ffts_dispatcher_minimal.cpp
    src/direct_discrete_h2d.cpp
    src/async_pipeline_h2d.cpp
    src/ffts_h2d.cpp
    src/three_stage_h2d.cpp
    src/three_stage_h2d_huge.cpp
)

# Feasibility test
add_executable(feasibility_test tests/feasibility_test.cpp ${COMMON_SRC})
target_link_libraries(feasibility_test ascendcl runtime pthread dl)

# Performance benchmark
add_executable(performance_benchmark tests/performance_benchmark.cpp ${COMMON_SRC})
target_link_libraries(performance_benchmark ascendcl runtime pthread dl)

# Enable testing
enable_testing()
add_test(NAME FeasibilityTest COMMAND feasibility_test)
add_test(NAME PerformanceBenchmark COMMAND performance_benchmark)