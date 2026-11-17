cmake_minimum_required(VERSION 3.22)
set(TCL_VERSION "0.1.0" CACHE STRING "Version (MAJOR.MINOR.PATCH)")
project(tcl_mupdfv VERSION ${TCL_VERSION} LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Static CRT: no VC++ redistributable needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(TCL_BUILD_TESTS "Build tests" ON)

include(cmake/mupdf.cmake)

# ---------------------------------------------------------------- plugin (Windows)
if(WIN32)
  enable_language(RC)
  add_library(tcl-mupdfv SHARED
    src/exports.cpp
    src/Document.cpp
    src/Viewer.cpp
    src/plugin.rc
    src/plugin.def)
  target_link_libraries(tcl-mupdfv PRIVATE mupdf_min user32 gdi32)
  target_compile_definitions(tcl-mupdfv PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/gen/version.h CONTENT
    "#define TCL_VERSION_STR \"${PROJECT_VERSION}\"\n#define TCL_VERSION_RC ${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0\n")
  target_include_directories(tcl-mupdfv PRIVATE ${CMAKE_BINARY_DIR}/gen)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_suffix ".wlx64")
  else()
    set(_suffix ".wlx")
  endif()
  set_target_properties(tcl-mupdfv PROPERTIES PREFIX "" SUFFIX ${_suffix})
  if(MSVC)
    target_compile_options(tcl-mupdfv PRIVATE /W4 /permissive- /utf-8 /EHsc /Gy /Gw)
    target_link_options(tcl-mupdfv PRIVATE /OPT:REF /OPT:ICF)
  else()
    target_compile_options(tcl-mupdfv PRIVATE -Wall -Wextra -Wno-missing-field-initializers -ffunction-sections -fdata-sections)
    target_link_options(tcl-mupdfv PRIVATE -static -static-libgcc -static-libstdc++
      -Wl,--gc-sections -Wl,--kill-at $<$<CONFIG:Release>:-s>)
  endif()

  # build/package/: the files to install, plus build/tcl-mupdfv-<ver>-<arch>.zip
  set(_arch $<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,x64,x86>)
  set(_pkg ${CMAKE_BINARY_DIR}/package)
  add_custom_target(package_zip
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_pkg}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_pkg}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:tcl-mupdfv> ${CMAKE_SOURCE_DIR}/dist/pluginst.inf
            ${CMAKE_SOURCE_DIR}/README.md ${CMAKE_SOURCE_DIR}/LICENSE ${_pkg}/
    COMMAND ${CMAKE_COMMAND} -E chdir ${_pkg} ${CMAKE_COMMAND} -E tar cf
            ${CMAKE_BINARY_DIR}/tcl-mupdfv-${PROJECT_VERSION}-${_arch}.zip --format=zip .
    DEPENDS tcl-mupdfv)
endif()

# ---------------------------------------------------------------- tests
if(TCL_BUILD_TESTS)
  enable_testing()
  # Core: renders the sample PDFs with the trimmed MuPDF and checks the warnings.
  add_executable(core_test tests/core_test.c)
  target_link_libraries(core_test PRIVATE mupdf_min)
  if(NOT WIN32)
    target_link_libraries(core_test PRIVATE m)
  endif()
  if(MINGW)
    target_link_options(core_test PRIVATE -static)
  endif()
  add_test(NAME core_test COMMAND core_test ${CMAKE_SOURCE_DIR}/tests/samples)
  if(NOT WIN32)
    # No Windows fonts here: use MuPDF's URW fonts under the Windows file names.
    set(_tf ${CMAKE_BINARY_DIR}/testfonts)
    foreach(_pair cour:NimbusMonoPS-Regular couri:NimbusMonoPS-Italic courbd:NimbusMonoPS-Bold
        courbi:NimbusMonoPS-BoldItalic arial:NimbusSans-Regular ariali:NimbusSans-Italic
        arialbd:NimbusSans-Bold arialbi:NimbusSans-BoldItalic times:NimbusRoman-Regular
        timesi:NimbusRoman-Italic timesbd:NimbusRoman-Bold timesbi:NimbusRoman-BoldItalic)
      string(REPLACE ":" ";" _pair ${_pair})
      list(GET _pair 0 _win)
      list(GET _pair 1 _urw)
      configure_file(${MU}/resources/fonts/urw/${_urw}.cff ${_tf}/${_win}.ttf COPYONLY)
    endforeach()
    set_tests_properties(core_test PROPERTIES ENVIRONMENT TCL_MUPDFV_FONT_DIR=${_tf})
  endif()

  if(WIN32)
    # Loads the real plugin DLL in a hidden window and drives it.
    add_executable(plugin_smoke tests/plugin_smoke.cpp)
    target_link_libraries(plugin_smoke PRIVATE user32 gdi32)
    target_compile_definitions(plugin_smoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MINGW)
      target_link_options(plugin_smoke PRIVATE -municode -static)
    endif()
    add_dependencies(plugin_smoke tcl-mupdfv)
    add_test(NAME plugin_smoke COMMAND plugin_smoke $<TARGET_FILE:tcl-mupdfv> ${CMAKE_SOURCE_DIR}/tests/samples)
  endif()
endif()
