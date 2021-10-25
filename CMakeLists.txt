cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(waveform)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF) # static link cpu_features

if(WIN32)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
endif()
find_package(LibObs REQUIRED)

if(MSVC)
    add_definitions(/MP) #parallel builds

    set(FFTW_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/deps/fftw-3.3.10/include")
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(FFTW_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/deps/fftw-3.3.10/x86/fftw3f.lib")
    else()
        set(FFTW_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/deps/fftw-3.3.10/x64/fftw3f.lib")
    endif()
else()
    find_path(FFTW_INCLUDE_DIRS fftw3.h)
    find_library(FFTW_LIBRARIES fftw3)
endif()

set(BUILD_TESTING OFF)
add_subdirectory(deps/cpu_features)

set(PLUGIN_SOURCES
    "src/module.hpp"
    "src/module.cpp"
    "src/source.hpp"
    "src/source.cpp"
    "src/aligned_mem.hpp"
    "src/math_funcs.hpp"
    "src/settings.hpp"
)

add_library(waveform MODULE ${PLUGIN_SOURCES})
target_include_directories(waveform PRIVATE ${LIBOBS_INCLUDE_DIRS} ${FFTW_INCLUDE_DIRS})
target_link_libraries(waveform PRIVATE ${LIBOBS_LIBRARIES} ${FFTW_LIBRARIES} cpu_features)
if(MSVC)
    target_compile_options(waveform PRIVATE "/W4") # warning level
endif()
