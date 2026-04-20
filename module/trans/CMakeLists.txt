add_executable(trans trans_main.cc)
target_include_directories(trans PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(trans PRIVATE fmt::fmt)
if (CUDA_FOUND)
elseif(ASCEND_FOUND)
else()
    target_sources(trans PRIVATE
        simu/trans_runtime_simu.cc
        simu/trans_case_simu.cc
    )
endif()
