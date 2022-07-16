# CMakeList.txt : CMake project for pi-camera-demo, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

include(GNUInstallDirs)

# Add source to this project's executable.
add_executable (pi-camera-demo "pi-camera-demo.cpp" "pi-camera-demo.h" "drm.hpp" "drm.cpp" "stream_info.hpp")

target_link_libraries(pi-camera-demo ${LIBCAMERA_LINK_LIBRARIES} fmt ${LIBDRM_LIBRARIES})
# TODO: Add tests and install targets if needed.