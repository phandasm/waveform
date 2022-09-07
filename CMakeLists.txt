cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckSymbolExists)

project(waveform)

set(WAVEFORM_VERSION "1.5.0-beta1")

# set default install prefix to OBS plugin path on linux and MacOS
# otherwise set it to the windows installer source folder
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(APPLE)
        set(CMAKE_INSTALL_PREFIX "/Library/Application Support/obs-studio/plugins" CACHE PATH "Default install path" FORCE)
    elseif(WIN32)
        set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/installer/bin" CACHE PATH "Default install path" FORCE)
    else()
        set(CMAKE_INSTALL_PREFIX "~/.config/obs-studio/plugins" CACHE PATH "Default install path" FORCE)
    endif()
endif()

include(GNUInstallDirs)

if(UNIX)
    option(PACKAGED_INSTALL "Use folder structure for package managers and system-wide installs" OFF)
    option(MAKE_DEB "Package as .deb" OFF)
endif()

option(DISABLE_X86_SIMD "Disable x86 SIMD optimizations" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF) # static link dependencies
if(NOT MSVC)
    option(STATIC_FFTW "Static link FFTW" OFF) # allow static linking FFTW on non-windows platforms
    option(BUILTIN_FFTW "Build FFTW from source" OFF)
endif()

# OSX bundles
if(APPLE)
    option(MAKE_BUNDLE "Make Mac OSX bundle" OFF)
endif()

# link OBS
find_package(libobs)
if(NOT TARGET OBS::libobs)
    message(WARNING "No modern OBS target found, trying fallback method.")
    find_package(LibObs REQUIRED)

    # emulate modern target
    add_library(OBS::libobs INTERFACE IMPORTED GLOBAL)
    target_link_libraries(OBS::libobs INTERFACE ${LIBOBS_LIBRARIES})
    target_include_directories(OBS::libobs INTERFACE ${LIBOBS_INCLUDE_DIRS})
endif()

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
    if(WIN32)
        option(WITH_COMBINED_THREADS "Merge thread library" ON)
    endif()
    option(ENABLE_FLOAT "single-precision" ON)
    if(NOT MSVC AND NOT DISABLE_X86_SIMD)
        option(ENABLE_SSE "Compile with SSE instruction set support" ON)
        option(ENABLE_SSE2 "Compile with SSE2 instruction set support" ON)
        #option(ENABLE_AVX "Compile with AVX instruction set support" ON) # i think this results in global VEX when building with CMake
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
    "src/log.hpp"
)

if(NOT DISABLE_X86_SIMD)
    list(APPEND PLUGIN_SOURCES
        "src/source_avx2.cpp"
        "src/source_avx.cpp"
        "src/filter_fma3.cpp"
        "src/aligned_mem.hpp"
    )

    # arch flags
    if(MSVC)
        set_source_files_properties("src/source_avx.cpp" PROPERTIES COMPILE_FLAGS "/arch:AVX")
        set_source_files_properties("src/source_avx2.cpp" PROPERTIES COMPILE_FLAGS "/arch:AVX2")
        set_source_files_properties("src/filter_fma3.cpp" PROPERTIES COMPILE_FLAGS "/arch:AVX")
    else()
        set_source_files_properties("src/source_avx.cpp" PROPERTIES COMPILE_FLAGS "-mavx -mfma")
        set_source_files_properties("src/source_avx2.cpp" PROPERTIES COMPILE_FLAGS "-mavx2 -mfma")
        set_source_files_properties("src/filter_fma3.cpp" PROPERTIES COMPILE_FLAGS "-mavx -mfma")
    endif()

    add_subdirectory(deps/cpu_features EXCLUDE_FROM_ALL)
    if(NOT MSVC AND NOT BUILD_SHARED_LIBS) # Clang complains
        target_compile_options(cpu_features PRIVATE "-fPIC")
    endif()
endif()

# collect all the locale files to install
file(GLOB LOCALE_FILES "data/locale/*.ini")

if(MAKE_BUNDLE)
    set_source_files_properties(${LOCALE_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/locale")
    set_source_files_properties("data/gradient.effect" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND PLUGIN_SOURCES ${LOCALE_FILES} "data/gradient.effect")

    # these settings stolen from https://github.com/obsproject/obs-plugintemplate/blob/0f60ca33f95905f248b9dd92b3f504921b823b4d/cmake/ObsPluginHelpers.cmake#L353
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_SKIP_BUILD_RPATH OFF)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks/")
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH OFF)
endif()

add_library(waveform MODULE ${PLUGIN_SOURCES})
set_target_properties(waveform PROPERTIES PREFIX "")
target_include_directories(waveform PRIVATE ${FFTW_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(waveform PRIVATE OBS::libobs ${FFTW_LIBRARIES})
if(NOT DISABLE_X86_SIMD)
    target_link_libraries(waveform PRIVATE cpu_features)
endif()
target_compile_definitions(waveform PRIVATE _USE_MATH_DEFINES)
if(MSVC)
    target_compile_options(waveform PRIVATE "/W4") # warning level
else()
    target_compile_options(waveform PRIVATE "-Wall" "-Wextra")
endif()

# OSX bundles
if(APPLE)
    target_compile_options(waveform PRIVATE "-stdlib=libc++")
    if(MAKE_BUNDLE)
        set_target_properties(waveform PROPERTIES
            BUNDLE ON
            BUNDLE_EXTENSION "plugin"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.github.phandasm.waveform"
            MACOSX_BUNDLE_BUNDLE_NAME "waveform"
            MACOSX_BUNDLE_BUNDLE_VERSION ${WAVEFORM_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${WAVEFORM_VERSION}
            MACOSX_BUNDLE_LONG_VERSION_STRING ${WAVEFORM_VERSION}
            MACOSX_BUNDLE_COPYRIGHT "GPLv3"
            MACOSX_BUNDLE_INFO_STRING "Audio visualization plugin for OBS Studio"
        )
    endif()
endif()

set(CMAKE_REQUIRED_LIBRARIES OBS::libobs)
check_symbol_exists(obs_properties_add_color_alpha "obs-module.h" HAVE_OBS_PROP_ALPHA)
configure_file("src/waveform_config.hpp.in" "include/waveform_config.hpp")
if(WIN32)
    configure_file("installer/installer.iss.in" "${CMAKE_CURRENT_SOURCE_DIR}/installer/installer.iss" @ONLY)
endif()

if(WIN32)
    install(TARGETS waveform DESTINATION "obs-plugins/$<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,64bit,32bit>")
    install(FILES $<TARGET_PDB_FILE:waveform> DESTINATION "obs-plugins/$<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,64bit,32bit>" OPTIONAL)
    install(FILES "data/gradient.effect" DESTINATION "data/obs-plugins/waveform")
    install(FILES ${LOCALE_FILES} DESTINATION "data/obs-plugins/waveform/locale")
else()
    if(PACKAGED_INSTALL OR MAKE_DEB)
        install(TARGETS waveform DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins")
        install(FILES "data/gradient.effect" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/waveform")
        install(FILES ${LOCALE_FILES} DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/waveform/locale")
    else()
        if(APPLE)
            if(MAKE_BUNDLE)
                install(TARGETS waveform DESTINATION ${CMAKE_INSTALL_PREFIX})
            else()
                install(TARGETS waveform DESTINATION "waveform/bin")
                install(FILES "data/gradient.effect" DESTINATION "waveform/data/")
                install(FILES ${LOCALE_FILES} DESTINATION "waveform/data/locale")
            endif()
        else()
            install(TARGETS waveform DESTINATION "waveform/bin/$<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,64bit,32bit>")
            install(FILES "data/gradient.effect" DESTINATION "waveform/data/")
            install(FILES ${LOCALE_FILES} DESTINATION "waveform/data/locale")
        endif()
    endif()
endif()

if(MAKE_DEB)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_deb.cmake")
elseif(APPLE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_macos.cmake")
endif()
