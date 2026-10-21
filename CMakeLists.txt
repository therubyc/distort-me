cmake_minimum_required(VERSION 3.16)
project(DistortMe VERSION 0.1.0 LANGUAGES C CXX)

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

# JavaScript port used by the panel's preview (needs Node.js)
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME js-engine COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/js/test_engine.js)
endif()

# --- Photoshop filter plug-in ----------------------------------------------
# Needs Adobe's Photoshop SDK, which is not part of this repository.
# Point PHOTOSHOP_SDK_DIR at the folder that contains "pluginsdk", e.g.
#   cmake -S . -B build -DPHOTOSHOP_SDK_DIR=C:/sdk/adobe_photoshop_sdk_2026_win_v2
set(PHOTOSHOP_SDK_DIR "" CACHE PATH "Folder of the Adobe Photoshop SDK (contains pluginsdk)")

if(PHOTOSHOP_SDK_DIR)
  set(PS_API "${PHOTOSHOP_SDK_DIR}/pluginsdk/photoshopapi")
  if(NOT EXISTS "${PS_API}/photoshop/PIFilter.h")
    message(FATAL_ERROR "PIFilter.h not found under ${PS_API}/photoshop - check PHOTOSHOP_SDK_DIR")
  endif()

  set(PLUGIN_COMMON
    plugin/common/DistortMe.cpp
    plugin/common/DistortMeMap.cpp
    plugin/common/DistortMeParams.cpp
    plugin/common/DistortMeScripting.cpp
  )

  if(WIN32)
    add_library(distortme-plugin SHARED
      ${PLUGIN_COMMON}
      plugin/win/DistortMeUIWin.cpp
      plugin/win/DistortMe.rc
    )
    set_target_properties(distortme-plugin PROPERTIES
      OUTPUT_NAME "DistortMe"
      PREFIX ""
      SUFFIX ".8bf")
    target_link_libraries(distortme-plugin PRIVATE comdlg32 user32)
    target_compile_definitions(distortme-plugin PRIVATE UNICODE _UNICODE _WINDOWS WIN32=1 _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
      # static runtime: no Visual C++ redistributable needed on the user's PC
      set_property(TARGET distortme-plugin distortme PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
      target_compile_options(distortme-plugin PRIVATE -Wno-multichar)
      target_link_options(distortme-plugin PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
  elseif(APPLE)
    enable_language(OBJCXX)
    set_source_files_properties(plugin/mac/DistortMeUIMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    add_library(distortme-plugin MODULE
      ${PLUGIN_COMMON}
      plugin/mac/DistortMeUIMac.mm
      plugin/PiPL.json
    )
    set_source_files_properties(plugin/PiPL.json PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(distortme-plugin PROPERTIES
      OUTPUT_NAME "DistortMe"
      BUNDLE TRUE
      BUNDLE_EXTENSION "plugin"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.therubyc.distortme"
      MACOSX_BUNDLE_BUNDLE_NAME "Distort Me"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    target_link_libraries(distortme-plugin PRIVATE "-framework Cocoa")
  endif()

  if(TARGET distortme-plugin)
    enable_language(CXX)
    set_target_properties(distortme-plugin PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
    target_include_directories(distortme-plugin PRIVATE
      plugin/common
      plugin/win
      third_party/stb
      "${PS_API}/photoshop"
      "${PS_API}/pica_sp"
    )
    target_link_libraries(distortme-plugin PRIVATE distortme)
    if(NOT MSVC)
      set_property(TARGET distortme PROPERTY POSITION_INDEPENDENT_CODE ON)
    endif()
  endif()
endif()

# Fake Photoshop host for testing the plug-in (Windows / Wine only).
if(PHOTOSHOP_SDK_DIR AND WIN32)
  add_executable(pshost tests/pshost/pshost.cpp)
  target_include_directories(pshost PRIVATE third_party/stb "${PS_API}/photoshop" "${PS_API}/pica_sp")
  target_compile_definitions(pshost PRIVATE _WINDOWS WIN32=1 _CRT_SECURE_NO_WARNINGS)
  if(NOT MSVC)
    target_compile_options(pshost PRIVATE -Wno-multichar)
    target_link_options(pshost PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()
