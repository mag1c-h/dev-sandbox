add_executable(copy copy_main.cc)
target_include_directories(copy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(copy PRIVATE fmt::fmt)

# Select implementation based on runtime backend
if(RUNTIME_BACKEND STREQUAL "CUDA")
    file(GLOB RUNTIME_SOURCE_FILES "cuda/*.cc" "cuda/*.cu" "cuda/*.h")
    target_link_libraries(copy PRIVATE CUDA::Runtime)
elseif(RUNTIME_BACKEND STREQUAL "Ascend")
    file(GLOB RUNTIME_SOURCE_FILES "ascend/*.cc" "ascend/*.h")
    target_link_libraries(copy PRIVATE Ascend::Runtime)
else()
    file(GLOB RUNTIME_SOURCE_FILES "simu/*.cc" "simu/*.h")
endif()

target_sources(copy PRIVATE ${RUNTIME_SOURCE_FILES})
