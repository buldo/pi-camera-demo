// pi-camera-demo.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <iostream>
#include <libcamera/request.h>
#include <libcamera/controls.h>
#include <libcamera/camera.h>

#include "stream_info.hpp"

// TODO: Reference additional headers your program requires here.
static void check_camera_stack();
void OpenCamera();
void ConfigureVideo();
void StartCamera();
void SetupCapture();
void MakeRequests();
void RequestComplete(libcamera::Request* request);


void DoneCallback(int fd);

StreamInfo GetStreamInfo(libcamera::Stream const* stream);
std::vector<libcamera::Span<uint8_t>> Mmap(libcamera::FrameBuffer* buffer);

struct CompletedRequest
{
    using BufferMap = libcamera::Request::BufferMap;
    using ControlList = libcamera::ControlList;
    using Request = libcamera::Request;

    CompletedRequest(Request* r)
        : buffers(r->buffers()), metadata(r->metadata()), request(r)
    {
        r->reuse();
    }

    BufferMap buffers;
    ControlList metadata;
    Request* request;
};