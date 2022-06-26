cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CheckSymbolExists)

project(waveform)

# set default install prefix to OBS plugin path on linux
# otherwise set it to the windows installer source folder
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(UNIX)
        set(CMAKE_INSTALL_PREFIX "~/.config/obs-studio/plugins" CACHE PATH "default install path" FORCE)
        option(PACKAGED_INSTALL "Use folder structure for package managers and system-wide installs" OFF)
    elseif(WIN32)
        set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/installer/bin" CACHE PATH "default install path" FORCE)
    endif()
endif()

option(DISABLE_X86_SIMD "Disable x86 SIMD optimizations" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF) # static link dependencies
option(NO_LIB_PREFIX "Don't add the lib prefix to output filename" OFF)
if(NOT MSVC)
    option(STATIC_FFTW "Static link FFTW" OFF) # allow static linking FFTW on non-windows platforms
    option(BUILTIN_FFTW "Build FFTW from source" OFF)
endif()

if(WIN32 OR APPLE)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
endif()
find_package(LibObs REQUIRED)

if(MSVC)
    add_definitions(/MP) # parallel builds

    option(EXTRA_OPTIMIZATIONS "Enable additional compiler optimizations" OFF)
    if(EXTRA_OPTIMIZATIONS)
        add_compile_options("$<$<CONFIG:Release>:/GL>" "$<$<CONFIG:Release>:/Oi>")
        add_link_options("$<$<CONFIG:Release>:/LTCG>")
    endif()

    # enable SSE2 globally for 32-bit build
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_compile_options("/arch:SSE2")
    endif()

    # enable PDB for release builds
    add_link_options("$<$<CONFIG:Release>:/DEBUG:FULL>")
    
    # allow static linked CRT
    option(STATIC_RUNTIME "Static link CRT" OFF)
    if(STATIC_RUNTIME)
        foreach(flag
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO
            CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
            CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)
            if(${flag} MATCHES "/MD") # older versions of CMake used to specify this by default, paranoia check
                string(REGEX REPLACE "/MD" "" ${flag} "${${flag}}")
            endif()
        endforeach()
        add_compile_options("$<$<CONFIG:Release>:/MT>") # release builds only
    endif()

    # build fftw
    # fftw's CMakeLists mishandles MSVC arch flags
    # so we will skip the autodetection and clear the flags
    # since they are not needed nor desired for a multiarch binary
    set(HAVE_SSE TRUE)
    set(SSE_FLAG "")
    set(HAVE_SSE2 TRUE)
    set(SSE2_FLAG "")
    set(HAVE_AVX TRUE)
    set(AVX_FLAG "")
    set(HAVE_AVX2 TRUE)
    set(AVX2_FLAG "")

    set(BUILTIN_FFTW TRUE)
endif()

if(BUILTIN_FFTW)    
    option(ENABLE_THREADS "Use pthread for multithreading" ON)
    if(WIN32 OR APPLE)
        option(WITH_COMBINED_THREADS "Merge thread library" ON)
    endif()
    option(ENABLE_FLOAT "single-precision" ON)
    if(NOT MSVC AND NOT DISABLE_X86_SIMD) # enable FFTW AVX support by default for GCC/Clang builds
        option(ENABLE_SSE "Compile with SSE instruction set support" ON)
        option(ENABLE_SSE2 "Compile with SSE2 instruction set support" ON)
        option(ENABLE_AVX "Compile with AVX instruction set support" ON)
    endif()
    add_subdirectory("deps/fftw-3.3.10" EXCLUDE_FROM_ALL)
    if(NOT MSVC AND NOT BUILD_SHARED_LIBS)
        target_compile_options(fftw3f PRIVATE "-fPIC") # GCC complains
    endif()
    set(FFTW_LIBRARIES fftw3f)
    set(FFTW_INCLUDE_DIRS "deps/fftw-3.3.10/api")
else()
    find_path(FFTW_INCLUDE_DIRS fftw3.h)
    if(STATIC_FFTW)
        find_library(FFTW_LIBRARIES libfftw3f.a)
    else()
        find_library(FFTW_LIBRARIES fftw3f)
    endif()
    if(NOT FFTW_INCLUDE_DIRS)
        message(FATAL_ERROR "Could not locate FFTW header.")
    endif()
    if(NOT FFTW_LIBRARIES)
        message(FATAL_ERROR "Could not locate FFTW library.")
    endif()
endif()

set(PLUGIN_SOURCES
    "src/module.hpp"
    "src/module.cpp"
    "src/source.hpp"
    "src/source.cpp"
    "src/source_generic.cpp"
    "src/membuf.hpp"
    "src/math_funcs.hpp"
    "src/filter.hpp"
    "src/settings.hpp"
)

if(NOT DISABLE_X86_SIMD)
    list(APPEND PLUGIN_SOURCES
        "src/source_avx2.cpp"
        "src/source_avx.cpp"
        "src/aligned_mem.hpp"
    )

    # arch flags
    if(MSVC)
        set_source_files_properties("src/source_avx.cpp" PROPERTIES COMPILE_FLAGS "/arch:AVX")
        set_source_files_properties("src/source_avx2.cpp" PROPERTIES COMPILE_FLAGS "/arch:AVX2")
    else()
        set_source_files_properties("src/source_avx.cpp" PROPERTIES COMPILE_FLAGS "-mavx")
        set_source_files_properties("src/source_avx2.cpp" PROPERTIES COMPILE_FLAGS "-mavx2")
    endif()

    add_subdirectory(deps/cpu_features EXCLUDE_FROM_ALL)
    if(NOT MSVC AND NOT BUILD_SHARED_LIBS) # Clang complains
        target_compile_options(cpu_features PRIVATE "-fPIC")
    endif()
endif()

add_library(waveform MODULE ${PLUGIN_SOURCES})
target_include_directories(waveform PRIVATE ${LIBOBS_INCLUDE_DIRS} ${FFTW_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(waveform PRIVATE ${LIBOBS_LIBRARIES} ${FFTW_LIBRARIES})
if(NOT DISABLE_X86_SIMD)
    target_link_libraries(waveform PRIVATE cpu_features)
endif()
target_compile_definitions(waveform PRIVATE _USE_MATH_DEFINES)
if(MSVC)
    target_compile_options(waveform PRIVATE "/W4") # warning level
else()
    target_compile_options(waveform PRIVATE "-Wall" "-Wextra")
    set(DECORATE_SIMD_FUNCS ON)
endif()
if(NO_LIB_PREFIX)
    set_target_properties(waveform PROPERTIES PREFIX "")
endif()

set(CMAKE_REQUIRED_INCLUDES ${LIBOBS_INCLUDE_DIRS})
set(CMAKE_REQUIRED_LIBRARIES ${LIBOBS_LIBRARIES})
check_symbol_exists(obs_properties_add_color_alpha "obs-module.h" HAVE_OBS_PROP_ALPHA)
configure_file("src/waveform_config.hpp.in" "include/waveform_config.hpp")

# collect all the locale files to install
file(GLOB LOCALE_FILES "data/locale/*.ini")

if(WIN32)
    install(TARGETS waveform DESTINATION "obs-plugins/$<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,64bit,32bit>")
    install(FILES $<TARGET_PDB_FILE:waveform> DESTINATION "obs-plugins/$<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,64bit,32bit>" OPTIONAL)
    #install(TARGETS fftw3f DESTINATION "obs-plugins/$<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,64bit,32bit>")
    install(FILES "data/gradient.effect" DESTINATION "data/obs-plugins/waveform")
    install(FILES ${LOCALE_FILES} DESTINATION "data/obs-plugins/waveform/locale")
else()
    if(PACKAGED_INSTALL)
        install(TARGETS waveform DESTINATION "lib/obs-plugins")
        install(FILES "data/gradient.effect" DESTINATION "share/obs/obs-plugins/waveform")
        install(FILES ${LOCALE_FILES} DESTINATION "share/obs/obs-plugins/waveform/locale")
    else()
        if(APPLE)
            install(TARGETS waveform DESTINATION "waveform/bin")
        else()
            install(TARGETS waveform DESTINATION "waveform/bin/$<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,64bit,32bit>")
        endif()
        install(FILES "data/gradient.effect" DESTINATION "waveform/data/")
        install(FILES ${LOCALE_FILES} DESTINATION "waveform/data/locale")
    endif()
endif()
