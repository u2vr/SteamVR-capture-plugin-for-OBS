cmake_minimum_required(VERSION 3.20)
project(obs-vr-mirror-by-u2vr VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(obs-vr-mirror-by-u2vr SHARED
    src/plugin-main.cpp
    src/stabilizer_math.h
    src/d3d11_stabilizer.h
    third_party/obs/obs_minimal.h
    third_party/openvr/openvr.h
)

target_include_directories(obs-vr-mirror-by-u2vr PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/obs
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr
)

target_compile_definitions(obs-vr-mirror-by-u2vr PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(obs-vr-mirror-by-u2vr PRIVATE /utf-8 /W3 /O2 /EHsc)
endif()

target_link_libraries(obs-vr-mirror-by-u2vr PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/obs/obs.lib"
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
)

set_target_properties(obs-vr-mirror-by-u2vr PROPERTIES
    PREFIX ""
    OUTPUT_NAME "obs-vr-mirror-by-u2vr"
)

add_custom_command(TARGET obs-vr-mirror-by-u2vr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist/obs-vr-mirror-by-u2vr/bin/64bit"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist/obs-vr-mirror-by-u2vr/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:obs-vr-mirror-by-u2vr>" "${CMAKE_CURRENT_SOURCE_DIR}/dist/obs-vr-mirror-by-u2vr/bin/64bit/obs-vr-mirror-by-u2vr.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/openvr_api.dll" "${CMAKE_CURRENT_SOURCE_DIR}/dist/obs-vr-mirror-by-u2vr/bin/64bit/openvr_api.dll"
)
