add_executable(rs_tests_parser test_parser.cpp)
target_link_libraries(rs_tests_parser PRIVATE rs_core)
add_test(NAME parser COMMAND rs_tests_parser)

add_executable(rs_tests_tokenizer test_tokenizer.cpp)
target_link_libraries(rs_tests_tokenizer PRIVATE rs_core)
add_test(NAME tokenizer COMMAND rs_tests_tokenizer)

add_executable(rs_tests_indexer test_indexer.cpp)
target_link_libraries(rs_tests_indexer PRIVATE rs_core)
add_test(NAME indexer COMMAND rs_tests_indexer)

add_executable(rs_tests_search test_search.cpp)
target_link_libraries(rs_tests_search PRIVATE rs_core)
add_test(NAME search COMMAND rs_tests_search)

add_executable(rs_tests_integration test_integration.cpp)
target_link_libraries(rs_tests_integration PRIVATE rs_core)
add_test(NAME integration COMMAND rs_tests_integration)

add_executable(rs_tests_relevance test_relevance.cpp)
target_link_libraries(rs_tests_relevance PRIVATE rs_core)
add_test(NAME relevance COMMAND rs_tests_relevance)
