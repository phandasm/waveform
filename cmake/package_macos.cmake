include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_common.cmake")

set(CPACK_PACKAGE_FILE_NAME "Waveform_v${WAVEFORM_VERSION}_MacOS_${CMAKE_OSX_ARCHITECTURES}")

set(CPACK_PRODUCTBUILD_IDENTIFIER "com.github.phandasm.waveform")

if(NOT DEFINED CPACK_PACKAGING_INSTALL_PREFIX)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/Library/Application Support/obs-studio/plugins")
endif()

set(CPACK_GENERATOR "productbuild")
include(CPack)