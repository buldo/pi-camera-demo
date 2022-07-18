#include "CameraWrapper.hpp"
#include <sys/mman.h>

#include <spdlog/spdlog.h>

#include <utility>

CameraWrapper::CameraWrapper(unsigned int width, unsigned int height)
{
    m_width = width;
    m_height = height;
    m_cameraManager = std::make_unique<libcamera::CameraManager>();
}

void CameraWrapper::Init(std::function<void(CameraWrapper*, libcamera::Request*)> processRequest)
{
    m_processRequest = std::move(processRequest);
    // Start camera manager
    m_cameraManager->start();

    // Get camera
    initCamera();
    initVideoStream();
    initCapture();
    initRequests();
}

void CameraWrapper::StartCapture()
{
    if (m_camera->start(&m_controls))
    {
        throw std::runtime_error("failed to start camera");
    }
    m_controls.clear();

    m_camera->requestCompleted.connect(this, &CameraWrapper::requestComplete);

    for (auto& request : m_requests)
    {
        if (m_camera->queueRequest(request.get()) < 0)
        {
            throw std::runtime_error("Failed to queue request");
        }
    }
}

std::vector<libcamera::Span<uint8_t>> CameraWrapper::Mmap(libcamera::FrameBuffer* buffer)
{
    auto item = m_cameraMappedBuffers.find(buffer);
    if (item == m_cameraMappedBuffers.end())
    {
        return {};
    }
    return item->second;
}

StreamInfo CameraWrapper::GetStreamInfo()
{
    return getStreamInfo(m_videoStream);
}

libcamera::Stream* CameraWrapper::GetVideoStream()
{
    return m_videoStream;
}

void CameraWrapper::ReuseRequest(libcamera::Request * request)
{
    request->reuse(libcamera::Request::ReuseFlag::ReuseBuffers);
    if (m_camera->queueRequest(request) < 0)
    {
        throw std::runtime_error("Failed to queue request");
    }
}

void CameraWrapper::initCamera()
{
    const auto cameraId = m_cameraManager->cameras()[0]->id();
    m_camera = m_cameraManager->get(cameraId);
    m_camera->acquire();
}

void CameraWrapper::initVideoStream()
{
    const libcamera::StreamRoles streamRoles = { libcamera::StreamRole::VideoRecording };
    m_cameraConfiguration = m_camera->generateConfiguration(streamRoles);

    auto& streamConfiguration = m_cameraConfiguration->at(0);
    streamConfiguration.pixelFormat = libcamera::formats::YUV420;
    streamConfiguration.bufferCount = BufferCount; // 6 buffers is better than 4
    streamConfiguration.size.width = m_width;
    streamConfiguration.size.height = m_height;
    streamConfiguration.colorSpace = libcamera::ColorSpace::Rec709;
}

void CameraWrapper::initCapture()
{
	auto validationStatus = m_cameraConfiguration->validate();
    if (validationStatus == libcamera::CameraConfiguration::Invalid)
    {
        throw std::runtime_error("failed to valid stream configurations");
    }
    if (validationStatus == libcamera::CameraConfiguration::Adjusted)
    {
        spdlog::error("Stream configuration adjusted");
    }

    if (m_camera->configure(m_cameraConfiguration.get()) < 0)
    {
        throw std::runtime_error("failed to configure streams");
    }

    // Next allocate all the buffers we need, mmap them and store them on a free list.

    m_cameraFrameBuffersAllocator = new libcamera::FrameBufferAllocator(m_camera);

    // There is only one stream in our application
    m_videoStream = m_cameraConfiguration->at(0).stream();

    if (m_cameraFrameBuffersAllocator->allocate(m_videoStream) < 0)
    {
        throw std::runtime_error("failed to allocate capture buffers");
    }

    for (const auto& buffer : m_cameraFrameBuffersAllocator->buffers(m_videoStream))
    {
        // "Single plane" buffers appear as multi-plane here, but we can spot them because then
        // planes all share the same fd. We accumulate them so as to mmap the buffer only once.
        size_t buffer_size = 0;
        for (unsigned i = 0; i < buffer->planes().size(); i++)
        {
            const auto& plane = buffer->planes()[i];
            buffer_size += plane.length;
            if (i == buffer->planes().size() - 1 || plane.fd.get() != buffer->planes()[i + 1].fd.get())
            {
                auto memory = mmap(nullptr, buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, plane.fd.get(), 0);
                m_cameraMappedBuffers[buffer.get()].push_back(
                    libcamera::Span<uint8_t>(static_cast<uint8_t*>(memory), buffer_size));
                buffer_size = 0;
            }
        }
        m_videoFrameBuffers.push(buffer.get());
    }

    spdlog::info("Buffers allocated and mapped");
}

void CameraWrapper::initRequests()
{
    auto free_buffers(m_videoFrameBuffers);

    // Creating an request for each buffer and assigning buffer to request
    while (free_buffers.empty() == false)
    {
        auto request = m_camera->createRequest();
        if (!request)
        {
            throw std::runtime_error("failed to make request");
        }

        const auto buffer = free_buffers.front();
        free_buffers.pop();
        if (request->addBuffer(m_videoStream, buffer) < 0)
        {
            throw std::runtime_error("failed to add buffer to request");
        }

        m_requests.push_back(std::move(request));
    }

    spdlog::info("Requests created");
}

void CameraWrapper::requestComplete(libcamera::Request* request)
{
    spdlog::info("Requests completed");
    if (request->status() == libcamera::Request::RequestCancelled)
    {
        return;
    }

    m_processRequest(this, request);
}

StreamInfo CameraWrapper::getStreamInfo(libcamera::Stream const* stream)
{
    auto const& cfg = stream->configuration();
    StreamInfo info;
    info.width = cfg.size.width;
    info.height = cfg.size.height;
    info.stride = cfg.stride;
    info.pixel_format = stream->configuration().pixelFormat;
    info.colour_space = stream->configuration().colorSpace;
    return info;
}