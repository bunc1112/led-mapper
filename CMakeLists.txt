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

# Builds one FFGL plugin module with the SDK compiled in.
function(add_ffgl_plugin NAME)
    add_library(${NAME} MODULE ${ARGN} ${ffgl_SOURCE_DIR}/source/lib/FFGLSDK.cpp)
    target_include_directories(${NAME} PRIVATE ${ffgl_SOURCE_DIR}/source/lib)
    if(WIN32)
        target_include_directories(${NAME} PRIVATE ${ffgl_SOURCE_DIR}/deps/glew-2.1.0/include)
        target_compile_definitions(${NAME} PRIVATE GLEW_STATIC _USRDLL)
        target_compile_options(${NAME} PRIVATE /utf-8 /bigobj)
        target_link_libraries(${NAME} PRIVATE
            ${ffgl_SOURCE_DIR}/deps/glew-2.1.0/lib/Release/x64/glew32s.lib opengl32)
        target_sources(${NAME} PRIVATE ${ffgl_SOURCE_DIR}/build/windows/FFGLPlugins.def)
        set_target_properties(${NAME} PROPERTIES PREFIX "" SUFFIX ".dll"
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(APPLE)
        find_library(OPENGL_FW OpenGL REQUIRED)
        target_link_libraries(${NAME} PRIVATE ${OPENGL_FW})
        set_target_properties(${NAME} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION bundle
            MACOSX_BUNDLE_BUNDLE_NAME ${NAME}
            MACOSX_BUNDLE_GUI_IDENTIFIER com.chronos.ffgl.${NAME}
            OSX_ARCHITECTURES "arm64;x86_64")
    else()
        # Linux: compile check only (Resolume does not run on Linux)
        target_include_directories(${NAME} PRIVATE ${ffgl_SOURCE_DIR}/deps/glew-2.1.0/include)
        target_compile_definitions(${NAME} PRIVATE GLEW_NO_GLU)
        set_target_properties(${NAME} PROPERTIES PREFIX "")
    endif()
endfunction()

# CHRONOS - 20 time-bending effects
add_ffgl_plugin(Chronos
    source/Chronos/Chronos.cpp source/Chronos/Chronos.h source/Chronos/Shaders.h)

# CUTUP - beat-synced recap montage with text
add_ffgl_plugin(Cutup
    source/Cutup/Cutup.cpp source/Cutup/Cutup.h source/Cutup/Engine.cpp source/Cutup/Engine.h
    source/Cutup/Shaders.h source/Cutup/FontAtlas.h)
