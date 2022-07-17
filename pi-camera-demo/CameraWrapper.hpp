#pragma once

#include <memory>
#include <queue>
#include <libcamera/libcamera.h>

class CameraWrapper
{
private:
    const unsigned int BufferCount = 6;

private:
    unsigned int m_width;
    unsigned int m_height;

    std::unique_ptr<libcamera::CameraManager> m_cameraManager = nullptr;
    std::shared_ptr<libcamera::Camera> m_camera = nullptr;
    std::unique_ptr<libcamera::CameraConfiguration> m_cameraConfiguration = nullptr;
    libcamera::FrameBufferAllocator* m_cameraFrameBuffersAllocator = nullptr;
    std::map<libcamera::FrameBuffer*, std::vector<libcamera::Span<uint8_t>>> m_cameraMappedBuffers;
    std::queue<libcamera::FrameBuffer*> m_videoFrameBuffers;
    std::vector<std::unique_ptr<libcamera::Request>> m_requests;
    libcamera::Stream* m_videoStream = nullptr;
    libcamera::ControlList m_controls = libcamera::controls::controls;

public:
    CameraWrapper(unsigned int width, unsigned int height);
    void Init();
    void StartCapture();

private:
    void initCamera();
    void initVideoStream();
    void initCapture();
    void initRequests();
    void requestComplete(libcamera::Request* request);
};
