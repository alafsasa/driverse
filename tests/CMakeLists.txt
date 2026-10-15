# Core logic tests

add_executable(test_types core/test_types.cpp)
target_link_libraries(test_types PRIVATE driverse_core)
add_test(NAME types COMMAND test_types)

add_executable(test_utils core/test_utils.cpp)
target_link_libraries(test_utils PRIVATE driverse_core)
add_test(NAME utils COMMAND test_utils)

add_executable(test_mounts core/test_mounts.cpp)
target_link_libraries(test_mounts PRIVATE driverse_core)
add_test(NAME mounts COMMAND test_mounts)

add_executable(test_scanner core/test_scanner.cpp)
target_link_libraries(test_scanner PRIVATE driverse_core)
add_test(NAME scanner COMMAND test_scanner)
