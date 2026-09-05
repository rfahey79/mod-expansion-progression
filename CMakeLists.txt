# AzerothCore discovers source files and conf/*.conf.dist automatically.
# Its generated loader calls Add<module_directory_name>Scripts().
# No legacy AC_ADD_SCRIPT registration is needed (or desirable).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(mod_progression_tests LANGUAGES CXX)
    enable_testing()
    add_subdirectory(tests)
endif()
