#pragma once

#include <functional>
#include <map>
#include <string>

#include <libcamera/base/span.h>
#include "stream_info.hpp"

class DrmPreview
{
private:
	struct Buffer
{
	Buffer() : fd(-1) {}
	int fd;
	size_t size;
	StreamInfo info;
	uint32_t bo_handle;
	unsigned int fb_handle;
};
	   void makeBuffer(int fd, size_t size, StreamInfo const& info, Buffer& buffer);
	   void findCrtc();
	   void findPlane();
	   int drmfd_;
	   int conId_;
	   uint32_t crtcId_;
	   int crtcIdx_;
	   uint32_t planeId_;
	   unsigned int out_fourcc_;
	   unsigned int x_;
	   unsigned int y_;
	   unsigned int width_;
	   unsigned int height_;
	   unsigned int screen_width_;
	   unsigned int screen_height_;
	   std::map<int, Buffer> buffers_; // map the DMABUF's fd to the Buffer
	   int last_fd_;
	   unsigned int max_image_width_;
	   unsigned int max_image_height_;
	   bool first_time_;


public:
	typedef std::function<void(int fd)> DoneCallback;

	DrmPreview();
	virtual ~DrmPreview() {}
	// This is where the application sets the callback it gets whenever the viewfinder
	// is no longer displaying the buffer and it can be safely recycled.
	void SetDoneCallback(DoneCallback callback) { done_callback_ = callback; }
	virtual void SetInfoText(const std::string& text) {}
	// Display the buffer. You get given the fd back in the BufferDoneCallback
	// once its available for re-use.

	// Reset the preview window, clearing the current buffers and being ready to
	// show new ones.

	// Check if preview window has been shut down.
	virtual bool Quit() { return false; }
	// Return the maximum image size allowed.


	// Display the buffer. You get given the fd back in the BufferDoneCallback
// once its available for re-use.
	void Show(int fd, libcamera::Span<uint8_t> span, StreamInfo const& info);
	// Reset the preview window, clearing the current buffers and being ready to
	// show new ones.
	void Reset();
	// Return the maximum image size allowed.
	void MaxImageSize(unsigned int& w, unsigned int& h) const
	{
		w = max_image_width_;
		h = max_image_height_;
	}

protected:
	DoneCallback done_callback_;
};

DrmPreview* make_preview();
