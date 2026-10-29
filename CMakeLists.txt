cmake_minimum_required(VERSION 3.15)
project(Chronos LANGUAGES C CXX)

# Pulls the official Resolume FFGL SDK (2.x, Resolume 7.3.1+)
include(FetchContent)
FetchContent_Declare(ffgl
    GIT_REPOSITORY https://github.com/resolume/ffgl.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_GetProperties(ffgl)
if(NOT ffgl_POPULATED)
    FetchContent_Populate(ffgl)   # source only - we compile the SDK ourselves
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(Chronos MODULE
    source/Chronos/Chronos.cpp
    source/Chronos/Chronos.h
    source/Chronos/Shaders.h
    ${ffgl_SOURCE_DIR}/source/lib/FFGLSDK.cpp)
target_include_directories(Chronos PRIVATE
    ${ffgl_SOURCE_DIR}/source/lib
    source/Chronos)

if(WIN32)
    target_include_directories(Chronos PRIVATE ${ffgl_SOURCE_DIR}/deps/glew-2.1.0/include)
    target_compile_definitions(Chronos PRIVATE GLEW_STATIC _USRDLL)
    target_link_libraries(Chronos PRIVATE
        ${ffgl_SOURCE_DIR}/deps/glew-2.1.0/lib/Release/x64/glew32s.lib opengl32)
    target_sources(Chronos PRIVATE ${ffgl_SOURCE_DIR}/build/windows/FFGLPlugins.def)
    set_target_properties(Chronos PROPERTIES PREFIX "" SUFFIX ".dll"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(APPLE)
    find_library(OPENGL_FW OpenGL REQUIRED)
    target_link_libraries(Chronos PRIVATE ${OPENGL_FW})
    set_target_properties(Chronos PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION bundle
        MACOSX_BUNDLE_BUNDLE_NAME Chronos
        MACOSX_BUNDLE_GUI_IDENTIFIER com.chronos.ffgl
        OSX_ARCHITECTURES "arm64;x86_64")
else()
    # Linux: compile check only (Resolume does not run on Linux)
    target_include_directories(Chronos PRIVATE ${ffgl_SOURCE_DIR}/deps/glew-2.1.0/include)
    target_compile_definitions(Chronos PRIVATE GLEW_NO_GLU)
    set_target_properties(Chronos PROPERTIES PREFIX "")
endif()
