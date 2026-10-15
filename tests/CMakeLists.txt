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

add_executable(test_recommendations core/test_recommendations.cpp)
target_link_libraries(test_recommendations PRIVATE driverse_core)
add_test(NAME recommendations COMMAND test_recommendations)

add_executable(test_config utils/test_config.cpp)
target_link_libraries(test_config PRIVATE driverse_core)
add_test(NAME config COMMAND test_config)

add_executable(test_audit utils/test_audit.cpp)
target_link_libraries(test_audit PRIVATE driverse_core)
add_test(NAME audit COMMAND test_audit)

add_executable(test_integration utils/test_integration.cpp)
target_link_libraries(test_integration PRIVATE driverse_core)
add_test(NAME integration COMMAND test_integration)

add_executable(test_logger utils/test_logger.cpp)
target_link_libraries(test_logger PRIVATE driverse_core)
add_test(NAME logger COMMAND test_logger)

# The bind-mount cycle guard (G1) can only be exercised with mount privileges.
# If unprivileged user+mount namespaces are available, re-run the scanner suite
# inside one so the bind-mount test actually executes instead of skipping.
add_test(NAME scanner_userns
         COMMAND ${CMAKE_SOURCE_DIR}/tests/run_userns.sh
                 $<TARGET_FILE:test_scanner>)
set_tests_properties(scanner_userns PROPERTIES SKIP_RETURN_CODE 77)
