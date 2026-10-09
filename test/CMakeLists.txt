add_executable(cpu_affinity_tests cpu_affinity_tests.cpp)
target_link_libraries(cpu_affinity_tests PRIVATE cpu_affinity)
add_test(NAME affinity_api COMMAND cpu_affinity_tests)
set_tests_properties(affinity_api PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME cli_help COMMAND cpu_affinity_demo --help)
add_test(NAME cli_list COMMAND cpu_affinity_demo --list)
add_test(NAME cli_unknown_option COMMAND cpu_affinity_demo --unknown)
add_test(NAME cli_missing_cpus COMMAND cpu_affinity_demo --cpus)
add_test(NAME cli_empty_cpus COMMAND cpu_affinity_demo --cpus "")
add_test(NAME cli_bad_cpus COMMAND cpu_affinity_demo --cpus 0,x)
add_test(NAME cli_trailing_comma COMMAND cpu_affinity_demo --cpus 0,)
add_test(NAME cli_duplicate_cpus COMMAND cpu_affinity_demo --cpus 0,0)
add_test(NAME cli_bad_seconds COMMAND cpu_affinity_demo --cpus 0 --seconds 0)
add_test(NAME cli_conflicting_options COMMAND cpu_affinity_demo --list --cpus 0)
add_test(NAME cli_negative_cpu COMMAND cpu_affinity_demo --cpus -1)
add_test(NAME cli_overflow_cpu COMMAND cpu_affinity_demo --cpus 4294967296)
add_test(NAME cli_duplicate_option COMMAND cpu_affinity_demo --cpus 0 --cpus 1)
add_test(NAME cli_seconds_without_cpus COMMAND cpu_affinity_demo --seconds 1)
set_tests_properties(
    cli_unknown_option cli_missing_cpus cli_empty_cpus cli_bad_cpus
    cli_trailing_comma cli_duplicate_cpus cli_bad_seconds cli_conflicting_options
    cli_negative_cpu cli_overflow_cpu cli_duplicate_option cli_seconds_without_cpus
    PROPERTIES WILL_FAIL TRUE)