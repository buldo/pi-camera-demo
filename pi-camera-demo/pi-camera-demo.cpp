// pi-camera-demo.cpp : Defines the entry point for the application.
//

#include "pi-camera-demo.h"
#include "drm.hpp"

#include <memory>
#include <libcamera/camera.h>
#include <libcamera/camera_manager.h>

#include <cstring>
#include <fcntl.h>

#include <sys/ioctl.h>

#include <cstring>
#include <unistd.h>
#include <libcamera/libcamera.h>
#include <linux/videodev2.h>
#include "spdlog/spdlog.h"

#include <memory>
#include "CameraWrapper.hpp"

// Default config
constexpr int WIDTH = 1280;
constexpr int HEIGHT = 720;

DrmPreview* preview;

std::unique_ptr<CameraWrapper> camera;

std::map<int, libcamera::Request*> fdToRequestMap;

int main()
{
    check_camera_stack();

    preview = new DrmPreview();
    preview->SetDoneCallback(&DoneCallback);

    camera = std::make_unique<CameraWrapper>(WIDTH, HEIGHT);
    camera->Init(ProcessRequest);
    camera->StartCapture();

    while(true){}

	return 0;
}

void ProcessRequest(CameraWrapper* cameraWrapper, libcamera::Request* request)
{
    const auto buffer = request->buffers().at(cameraWrapper->GetVideoStream());
    const auto& span = cameraWrapper->Mmap(buffer)[0];
    const auto info = cameraWrapper->GetStreamInfo();
    const auto fd = buffer->planes()[0].fd.get();
    fdToRequestMap[fd] = request;
    preview->Show(fd, span, info);
}


void DoneCallback(int fd)
{
    camera->ReuseRequest(fdToRequestMap.at(fd));
}


static void check_camera_stack()
{
    int fd = open("/dev/video0", O_RDWR, 0);
    if (fd < 0)
    {
        return;
    }

    v4l2_capability caps{};
    const int controlResult = ioctl(fd, VIDIOC_QUERYCAP, &caps);
    close(fd);

    if (controlResult < 0 || strcmp(reinterpret_cast<char*>(caps.driver), "bm2835 mmal") != 0)
    {
        return;
    }

    spdlog::error("ERROR: the system appears to be configured for the legacy camera stack");
    exit(-1);
}
