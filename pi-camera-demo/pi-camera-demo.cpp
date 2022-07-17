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
#include <cstdio>
#include <queue>
#include <unistd.h>
#include "spdlog/spdlog.h"
#include <libcamera/stream.h>
#include <libcamera/libcamera.h>
#include <cstring>

#include <sys/mman.h>

#include <memory>
#include <queue>
#include <set>
#include <string>

// Default config
constexpr int _width = 1280;
constexpr int _height = 720;

// camera
std::unique_ptr<libcamera::CameraManager> _cameraManager;
std::shared_ptr<libcamera::Camera> _camera;
std::unique_ptr<libcamera::CameraConfiguration> _cameraConfiguration;
libcamera::FrameBufferAllocator* _cameraFrameBuffersAllocator = nullptr;
std::queue<libcamera::FrameBuffer*> _videoFrameBuffers;
std::map<libcamera::FrameBuffer*, std::vector<libcamera::Span<uint8_t>>> _cameraMappedBuffers;
libcamera::FrameBufferAllocator* _frameBuffersAllocator;
std::vector<std::unique_ptr<libcamera::Request>> _requests;
libcamera::ControlList _controls = libcamera::controls::controls;

DrmPreview* _preview;
libcamera::Stream* _videoStream;

int main()
{
    check_camera_stack();

    _preview = make_preview();
    _preview->SetDoneCallback(&DoneCallback);

    _cameraManager = std::make_unique<libcamera::CameraManager>();
    const int cameraStartResult = _cameraManager->start();
    if (cameraStartResult)
    {
        throw std::runtime_error("camera manager failed to start, code " + std::to_string(-cameraStartResult));
    }

    OpenCamera();
    ConfigureVideo();
    SetupCapture();
    MakeRequests();
    StartCamera();

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

void OpenCamera()
{
    const auto cameraId = _cameraManager->cameras()[0]->id();
    _camera = _cameraManager->get(cameraId);
    _camera->acquire();
}

void ConfigureVideo()
{
    const libcamera::StreamRoles stream_roles = { libcamera::StreamRole::VideoRecording };
    _cameraConfiguration = _camera->generateConfiguration(stream_roles);

    // Now we get to override any of the default settings from the options_->
    libcamera::StreamConfiguration& cfg = _cameraConfiguration->at(0);
    cfg.pixelFormat = libcamera::formats::YUV420;
    cfg.bufferCount = 6; // 6 buffers is better than 4
    cfg.size.width = _width;
    cfg.size.height = _height;
    cfg.colorSpace = libcamera::ColorSpace::Rec709;
}

void StartCamera()
{
    _videoStream = _cameraConfiguration->at(0).stream();

     if (_camera->start(&_controls))
         throw std::runtime_error("failed to start camera");
     _controls.clear();

     _camera->requestCompleted.connect(&RequestComplete);

    for (std::unique_ptr<libcamera::Request>& request : _requests)
    {
        if (_camera->queueRequest(request.get()) < 0)
            throw std::runtime_error("Failed to queue request");
    }
}

void SetupCapture()
{
    libcamera::CameraConfiguration::Status validation = _cameraConfiguration->validate();
    if (validation == libcamera::CameraConfiguration::Invalid)
    {
        throw std::runtime_error("failed to valid stream configurations");
    }
    if (validation == libcamera::CameraConfiguration::Adjusted)
    {
        spdlog::error("Stream configuration adjusted");
    }

    if (_camera->configure(_cameraConfiguration.get()) < 0)
    {
        throw std::runtime_error("failed to configure streams");
    }

    // Next allocate all the buffers we need, mmap them and store them on a free list.

    _frameBuffersAllocator = new libcamera::FrameBufferAllocator(_camera);
    for (libcamera::StreamConfiguration& config : *_cameraConfiguration)
    {
        libcamera::Stream* stream = config.stream();

        if (_frameBuffersAllocator->allocate(stream) < 0)
            throw std::runtime_error("failed to allocate capture buffers");

        for (const std::unique_ptr<libcamera::FrameBuffer>& buffer : _frameBuffersAllocator->buffers(stream))
        {
            // "Single plane" buffers appear as multi-plane here, but we can spot them because then
            // planes all share the same fd. We accumulate them so as to mmap the buffer only once.
            size_t buffer_size = 0;
            for (unsigned i = 0; i < buffer->planes().size(); i++)
            {
                const libcamera::FrameBuffer::Plane& plane = buffer->planes()[i];
                buffer_size += plane.length;
                if (i == buffer->planes().size() - 1 || plane.fd.get() != buffer->planes()[i + 1].fd.get())
                {
                    void* memory = mmap(nullptr, buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, plane.fd.get(), 0);
                    _cameraMappedBuffers[buffer.get()].push_back(
                        libcamera::Span<uint8_t>(static_cast<uint8_t*>(memory), buffer_size));
                    buffer_size = 0;
                }
            }
            _videoFrameBuffers.push(buffer.get());
        }
    }

    spdlog::info("Buffers allocated and mapped");
}

void MakeRequests()
{
    auto free_buffers(_videoFrameBuffers);
    while (true)
    {
        for (libcamera::StreamConfiguration& config : *_cameraConfiguration)
        {
            libcamera::Stream* stream = config.stream();
            if (stream == _cameraConfiguration->at(0).stream())
            {
                if (free_buffers.empty())
                {
                    spdlog::info("Requests created");
                    return;
                }
                std::unique_ptr<libcamera::Request> request = _camera->createRequest();
                if (!request)
                    throw std::runtime_error("failed to make request");
                _requests.push_back(std::move(request));
            }
            else if (free_buffers.empty())
                throw std::runtime_error("concurrent streams need matching numbers of buffers");

            libcamera::FrameBuffer* buffer = free_buffers.front();
            free_buffers.pop();
            if (_requests.back()->addBuffer(stream, buffer) < 0)
                throw std::runtime_error("failed to add buffer to request");
        }
    }
}

void RequestComplete(libcamera::Request* request)
{
    spdlog::info("Requests completed");
    if (request->status() == libcamera::Request::RequestCancelled)
    {
        return;
    }

    // -------------------


    //if (item.stream->configuration().pixelFormat != libcamera::formats::YUV420)
    //    throw std::runtime_error("Preview windows only support YUV420");

    StreamInfo info = GetStreamInfo(_videoStream);
    libcamera::FrameBuffer* buffer = request->buffers().at(_videoStream);
    libcamera::Span span = Mmap(buffer)[0];

    int fd = buffer->planes()[0].fd.get();
    _preview->Show(fd, span, info);

    // -------------------


    libcamera::Request::BufferMap buffers(std::move(request->buffers()));

    request->reuse();

    for (const auto& p : buffers)
    {
        if (request->addBuffer(p.first, p.second) < 0)
            throw std::runtime_error("failed to add buffer to request in QueueRequest");
    }
    //

    // CompletedRequestPtr payload(r, [this](CompletedRequest* cr) { this->queueRequest(cr); });
    // {
    //     std::lock_guard<std::mutex> lock(_completedRequestsMutex);
    //     _completedRequests.insert(r);
    // }
    //
    // auto msg = Msg(MsgType::RequestComplete, payload);
    //
    // ReadyRequestsQueue->enqueue(msg);
}

void QueueRequest(CompletedRequest* completed_request)
{
    libcamera::Request::BufferMap buffers(std::move(completed_request->buffers));

    libcamera::Request* request = completed_request->request;
    delete completed_request;
    assert(request);





    /*{
        std::lock_guard<std::mutex> lock(_controlMutex);
        request->controls() = std::move(_controls);
    }*/

    if (_camera->queueRequest(request) < 0)
        throw std::runtime_error("failed to queue request");
}


std::vector<libcamera::Span<uint8_t>> Mmap(libcamera::FrameBuffer* buffer)
{
    auto item = _cameraMappedBuffers.find(buffer);
    if (item == _cameraMappedBuffers.end())
        return {};
    return item->second;
}

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