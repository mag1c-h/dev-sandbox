add_executable(copy copy_main.cc)
target_include_directories(copy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(copy PRIVATE fmt::fmt)

# Select implementation based on runtime backend
if(RUNTIME_BACKEND STREQUAL "CUDA")
    file(GLOB RUNTIME_SOURCE_FILES "cuda/*.cc" "cuda/*.cu" "cuda/*.h")
    target_link_libraries(copy PRIVATE CUDA::Runtime)

    # GPUDirect RDMA support (optional)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GDRCOPY QUIET gdrcopy)
        pkg_check_modules(IBVERBS QUIET libibverbs)

        if(GDRCOPY_FOUND AND IBVERBS_FOUND)
            message(STATUS "GPUDirect RDMA support enabled (gdrcopy + ibverbs)")
            file(GLOB GDR_SOURCE_FILES "cuda/gdr_*.cc" "cuda/copy_*_gdr*.cc")
            list(APPEND RUNTIME_SOURCE_FILES ${GDR_SOURCE_FILES})
            target_include_directories(copy PRIVATE
                ${GDRCOPY_INCLUDE_DIRS} ${IBVERBS_INCLUDE_DIRS})
            target_link_directories(copy PRIVATE
                ${GDRCOPY_LIBRARY_DIRS} ${IBVERBS_LIBRARY_DIRS})
            target_link_libraries(copy PRIVATE
                ${GDRCOPY_LIBRARIES} ${IBVERBS_LIBRARIES})
            target_compile_definitions(copy PRIVATE HAS_GPUDIRECT_RDMA=1)
        else()
            message(STATUS "GPUDirect RDMA disabled (gdrcopy or ibverbs not found)")
        endif()
    endif()
elseif(RUNTIME_BACKEND STREQUAL "Ascend")
    file(GLOB RUNTIME_SOURCE_FILES "ascend/*.cc" "ascend/*.h")
    target_link_libraries(copy PRIVATE Ascend::Runtime)
else()
    file(GLOB RUNTIME_SOURCE_FILES "simu/*.cc" "simu/*.h")
endif()

target_sources(copy PRIVATE ${RUNTIME_SOURCE_FILES})
