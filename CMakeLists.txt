# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.18)

project ("pi-camera-demo")

set (CMAKE_CXX_STANDARD 20)

find_package(PkgConfig REQUIRED)

# spdlog for logging
find_package(spdlog REQUIRED)

# libcamera for capture
pkg_check_modules(LIBCAMERA REQUIRED libcamera)
message(STATUS "libcamera library found:")
message(STATUS "    version: ${LIBCAMERA_VERSION}")
message(STATUS "    libraries: ${LIBCAMERA_LINK_LIBRARIES}")
message(STATUS "    include path: ${LIBCAMERA_INCLUDE_DIRS}")
include_directories(${CMAKE_SOURCE_DIR} ${LIBCAMERA_INCLUDE_DIRS} )

# Include sub-projects.
add_subdirectory ("pi-camera-demo")
