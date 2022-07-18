// pi-camera-demo.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <libcamera/camera.h>
#include <libcamera/controls.h>
#include <libcamera/request.h>

#include "CameraWrapper.hpp"


// TODO: Reference additional headers your program requires here.
static void check_camera_stack();
void ProcessRequest(CameraWrapper* cameraWrapper, libcamera::Request* request);
void DoneCallback(int fd);

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