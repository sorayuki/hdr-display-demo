cmake_minimum_required(VERSION 3.20.0)
project(p010-viewer VERSION 0.1.0 LANGUAGES C CXX)

add_executable(p010-viewer WIN32 main.cpp)
target_compile_definitions(p010-viewer PRIVATE UNICODE _UNICODE)
target_link_libraries(p010-viewer PRIVATE d3d11.lib dxgi.lib d2d1.lib)

# 启用 F16C 指令集支持
if(MSVC)
    target_compile_options(p010-viewer PRIVATE /arch:AVX2)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(p010-viewer PRIVATE -mf16c -mavx2)
endif()

set_target_properties(p010-viewer PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded"
)