// pi-camera-demo.cpp : Defines the entry point for the application.
//

#include "pi-camera-demo.h"
#include "drm.hpp"

#include <memory>
#include <libcamera/base/span.h>
#include <libcamera/camera.h>
#include <libcamera/camera_manager.h>
#include <libcamera/control_ids.h>
#include <libcamera/controls.h>
#include <libcamera/framebuffer_allocator.h>

#include <libcamera/pixel_format.h>
#include <cstring>
#include <fcntl.h>

#include <sys/ioctl.h>

#include <linux/videodev2.h>
#include <unistd.h>
#include "spdlog/spdlog.h"
#include <libcamera/stream.h>
#include <libcamera/libcamera.h>
#include <cstring>

#include <memory>
#include <string>
#include "CameraWrapper.hpp"

// Default config
constexpr int WIDTH = 1280;
constexpr int HEIGHT = 720;

DrmPreview* preview;

std::unique_ptr<CameraWrapper> camera;

int main()
{
    check_camera_stack();

    preview = make_preview();
    preview->SetDoneCallback(&DoneCallback);

    camera = std::make_unique<CameraWrapper>(WIDTH, HEIGHT);
    camera->Init();
    camera->StartCapture();

    while(true){}

	return 0;
}

void DoneCallback(int fd)
{

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

//void QueueRequest(CompletedRequest* completed_request)
//{
//    libcamera::Request::BufferMap buffers(std::move(completed_request->buffers));
//
//    libcamera::Request* request = completed_request->request;
//    delete completed_request;
//    assert(request);
//
//
//
//
//
//    /*{
//        std::lock_guard<std::mutex> lock(_controlMutex);
//        request->controls() = std::move(_controls);
//    }*/
//
//    if (_camera->queueRequest(request) < 0)
//        throw std::runtime_error("failed to queue request");
//}


//std::vector<libcamera::Span<uint8_t>> Mmap(libcamera::FrameBuffer* buffer)
//{
//    auto item = _cameraMappedBuffers.find(buffer);
//    if (item == _cameraMappedBuffers.end())
//        return {};
//    return item->second;
//}

StreamInfo GetStreamInfo(libcamera::Stream const* stream)
{
	libcamera::StreamConfiguration const& cfg = stream->configuration();
    StreamInfo info;
    info.width = cfg.size.width;
    info.height = cfg.size.height;
    info.stride = cfg.stride;
    info.pixel_format = stream->configuration().pixelFormat;
    info.colour_space = stream->configuration().colorSpace;
    return info;
}