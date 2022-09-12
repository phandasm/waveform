include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_common.cmake")

set(CPACK_PACKAGE_FILE_NAME "Waveform_v${WAVEFORM_VERSION}_Ubuntu_x86_64")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Devin Davila <daviladsoftware@gmail.com>")

#set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "obs-studio (>= 27.0.0)")

set(CPACK_GENERATOR "DEB")
include(CPack)