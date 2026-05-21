if(NOT TARGET CUDA::Runtime)
    message(STATUS "Skipping gdrbw: CUDA runtime target is unavailable")
    return()
endif()

find_path(IBVERBS_INCLUDE_PATH NAMES infiniband/verbs.h)
find_library(IBVERBS_LIB NAMES ibverbs)
find_package(Threads REQUIRED)

if(IBVERBS_INCLUDE_PATH AND IBVERBS_LIB)
    add_executable(gdrbw
        gdrbw.cc
        rdma_channel.cc
    )
    target_include_directories(gdrbw PRIVATE ${IBVERBS_INCLUDE_PATH})
    target_link_libraries(gdrbw PRIVATE fmt::fmt CUDA::Runtime ${IBVERBS_LIB} Threads::Threads)
else()
    message(STATUS "Skipping gdrbw: libibverbs headers or library were not found")
endif()
