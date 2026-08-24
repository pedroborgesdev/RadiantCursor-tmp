cmake_minimum_required(VERSION 3.20)
project(RadiantCursorRuntime VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "RadiantCursor.Runtime is a Windows-only target")
endif()

add_executable(RadiantCursor.Runtime WIN32
  src/main.cpp
  src/json.cpp
  engine/effectengine.cpp
  src/renderer.cpp
  resources.rc
)

target_compile_features(RadiantCursor.Runtime PRIVATE cxx_std_20)
target_compile_definitions(RadiantCursor.Runtime PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_include_directories(RadiantCursor.Runtime PRIVATE src engine)
set_target_properties(RadiantCursor.Runtime PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
)
target_link_libraries(RadiantCursor.Runtime PRIVATE
  d2d1 d3d11 dxgi dcomp dwrite dwmapi user32 gdi32 ole32 shell32 shlwapi
)

if(MSVC)
  target_compile_options(RadiantCursor.Runtime PRIVATE /W4 /permissive- /EHsc /utf-8)
  target_link_options(RadiantCursor.Runtime PRIVATE /SUBSYSTEM:WINDOWS)
else()
  target_compile_options(RadiantCursor.Runtime PRIVATE -Wall -Wextra -Wpedantic)
  target_link_options(RadiantCursor.Runtime PRIVATE -municode -static)
endif()

install(TARGETS RadiantCursor.Runtime RUNTIME DESTINATION runtime/windows)

include(CTest)
if(BUILD_TESTING)
  add_executable(RadiantCursor.EffectEngine.Tests
    tests/effectengine_test.cpp
    src/json.cpp
    engine/effectengine.cpp
  )
  target_compile_features(RadiantCursor.EffectEngine.Tests PRIVATE cxx_std_20)
  target_compile_definitions(RadiantCursor.EffectEngine.Tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(RadiantCursor.EffectEngine.Tests PRIVATE src engine)
  if(MSVC)
    target_compile_options(RadiantCursor.EffectEngine.Tests PRIVATE /W4 /permissive- /EHsc /utf-8 /UNDEBUG)
  else()
    target_compile_options(RadiantCursor.EffectEngine.Tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  endif()
  add_test(NAME windows-effectengine COMMAND RadiantCursor.EffectEngine.Tests)
endif()
