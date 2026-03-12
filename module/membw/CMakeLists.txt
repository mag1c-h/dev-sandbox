file(GLOB_RECURSE MEMBW_SOURCE_FILES "*.h" "*.cc")
add_executable(membw ${MEMBW_SOURCE_FILES})
target_link_libraries(membw PRIVATE fmt::fmt)
