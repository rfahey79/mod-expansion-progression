# Keep test sources as .in files so AzerothCore's recursive source collector
# cannot accidentally link a test main() or mock headers into worldserver.
configure_file(CoreStubs.h.in CoreStubs.h COPYONLY)
foreach(header Chat CommandScript Config Log ObjectAccessor Player PlayerScript World WorldScript)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${header}.h" "#include \"CoreStubs.h\"\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/GlobalScript.h" "#include \"CoreStubs.h\"\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/LFG.h" "#include \"CoreStubs.h\"\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/LFGMgr.h" "#include \"CoreStubs.h\"\n")
configure_file(progression_tests.cpp.in progression_tests.cpp COPYONLY)
add_executable(progression_tests "${CMAKE_CURRENT_BINARY_DIR}/progression_tests.cpp")
target_compile_features(progression_tests PRIVATE cxx_std_17)
target_include_directories(progression_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME progression_behavior COMMAND progression_tests)
