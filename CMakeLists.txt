cmake_minimum_required(VERSION 3.16)
project(DistortMe VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- core library: no dependencies, no Photoshop --------------------------
add_library(distortme STATIC
  src/distortme.c
  src/dm_threads.c
)
target_include_directories(distortme PUBLIC include)
if(MSVC)
  target_compile_options(distortme PRIVATE /W4 /fp:fast)
  target_compile_definitions(distortme PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(distortme PRIVATE -Wall -Wextra -O3)
  find_package(Threads REQUIRED)
  target_link_libraries(distortme PUBLIC Threads::Threads m)
endif()

# --- command line test tool -----------------------------------------------
add_executable(distortme-cli cli/main.c)
set_target_properties(distortme-cli PROPERTIES OUTPUT_NAME distortme)
target_include_directories(distortme-cli PRIVATE third_party/stb)
target_link_libraries(distortme-cli PRIVATE distortme)
if(MSVC)
  target_compile_definitions(distortme-cli PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# --- tests -----------------------------------------------------------------
enable_testing()
add_executable(distortme-tests tests/test_core.c)
target_link_libraries(distortme-tests PRIVATE distortme)
add_test(NAME core COMMAND distortme-tests)
