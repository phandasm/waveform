cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CheckSymbolExists)

project(waveform)

# set default install prefix to OBS plugin path on linux
if(UNIX AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "~/.config/obs-studio/plugins" CACHE PATH "default install path" FORCE)
endif()

option(BUILD_SHARED_LIBS "Build shared libraries" OFF) # static link cpu_features

if(WIN32)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
endif()
find_package(LibObs REQUIRED)

if(MSVC)
    add_definitions(/MP) # parallel builds

    set(FFTW_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/deps/fftw-3.3.10/include")
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(FFTW_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/deps/fftw-3.3.10/x86/fftw3f.lib")
        set(FFTW_DLL "${CMAKE_CURRENT_SOURCE_DIR}/deps/fftw-3.3.10/x86/fftw3f.dll")
    else()
        set(FFTW_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/deps/fftw-3.3.10/x64/fftw3f.lib")
        set(FFTW_DLL "${CMAKE_CURRENT_SOURCE_DIR}/deps/fftw-3.3.10/x64/fftw3f.dll")
    endif()
else()
    find_path(FFTW_INCLUDE_DIRS fftw3.h)
    find_library(FFTW_LIBRARIES fftw3f)
    if(NOT FFTW_INCLUDE_DIRS)
        message(FATAL_ERROR "Could not locate FFTW header.")
    endif()
    if(NOT FFTW_LIBRARIES)
        message(FATAL_ERROR "Could not locate FFTW library.")
    endif()
endif()

add_subdirectory(deps/cpu_features EXCLUDE_FROM_ALL)

set(PLUGIN_SOURCES
    "src/module.hpp"
    "src/module.cpp"
    "src/source.hpp"
    "src/source.cpp"
    "src/aligned_mem.hpp"
    "src/math_funcs.hpp"
    "src/filter.hpp"
    "src/settings.hpp"
)

add_library(waveform MODULE ${PLUGIN_SOURCES})
target_include_directories(waveform PRIVATE ${LIBOBS_INCLUDE_DIRS} ${FFTW_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(waveform PRIVATE ${LIBOBS_LIBRARIES} ${FFTW_LIBRARIES} cpu_features)
target_compile_definitions(waveform PRIVATE _USE_MATH_DEFINES)
if(MSVC)
    target_compile_options(waveform PRIVATE "/W4") # warning level
else()
    target_compile_options(waveform PRIVATE "-Wall" "-Wextra")
    set(DECORATE_SIMD_FUNCS ON)
endif()

set(CMAKE_REQUIRED_INCLUDES ${LIBOBS_INCLUDE_DIRS})
set(CMAKE_REQUIRED_LIBRARIES ${LIBOBS_LIBRARIES})
check_symbol_exists(obs_properties_add_color_alpha "obs-module.h" HAVE_OBS_PROP_ALPHA)
configure_file("src/waveform_config.hpp.in" "include/waveform_config.hpp")

if(WIN32)
    install(TARGETS waveform DESTINATION "obs-plugins/$<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,64bit,32bit>")
    install(FILES ${FFTW_DLL} DESTINATION "obs-plugins/$<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,64bit,32bit>")
    install(FILES "data/gradient.effect" DESTINATION "data/obs-plugins/waveform")
    install(FILES "data/locale/en-US.ini" DESTINATION "data/obs-plugins/waveform/locale")
else()
    install(TARGETS waveform DESTINATION "waveform/bin/$<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,64bit,32bit>")
    install(FILES "data/gradient.effect" DESTINATION "waveform/data/")
    install(FILES "data/locale/en-US.ini" DESTINATION "waveform/data/locale")
endif()
