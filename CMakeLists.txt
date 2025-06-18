cmake_minimum_required(VERSION 3.20.0)
project(hdr-gpt VERSION 0.1.0 LANGUAGES C CXX)

add_executable(hdr-gpt WIN32 main.cpp)
target_compile_definitions(hdr-gpt PRIVATE UNICODE _UNICODE)
target_link_libraries(hdr-gpt PRIVATE d3d11.lib dxgi.lib d2d1.lib)
set_target_properties(hdr-gpt PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded"
)