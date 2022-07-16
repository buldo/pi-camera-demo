/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2021, Raspberry Pi (Trading) Ltd.
 *
 * preview.cpp - preview window interface
 */

#include "drm.hpp"

#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <iostream>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <ostream>
#include <cstring>
#include <unistd.h>
#include <asm-generic/ioctl.h>
#include <libcamera/color_space.h>


DrmPreview* make_drm_preview();

DrmPreview* make_preview()
{
	DrmPreview* p = make_drm_preview();
	return p;
}

#define ERRSTR strerror(errno)

void DrmPreview::findCrtc()
{
	int i;
	drmModeRes* res = drmModeGetResources(drmfd_);
	if (!res)
		throw std::runtime_error("drmModeGetResources failed: " + std::string(ERRSTR));

	if (res->count_crtcs <= 0)
		throw std::runtime_error("drm: no crts");

	max_image_width_ = res->max_width;
	max_image_height_ = res->max_height;

	if (!conId_)
	{
		std::cerr << "No connector ID specified.  Choosing default from list:" << std::endl;

		for (i = 0; i < res->count_connectors; i++)
		{
			drmModeConnector* con = drmModeGetConnector(drmfd_, res->connectors[i]);
			drmModeEncoder* enc = NULL;
			drmModeCrtc* crtc = NULL;

			if (con->encoder_id)
			{
				enc = drmModeGetEncoder(drmfd_, con->encoder_id);
				if (enc->crtc_id)
				{
					crtc = drmModeGetCrtc(drmfd_, enc->crtc_id);
				}
			}

			if (!conId_ && crtc)
			{
				conId_ = con->connector_id;
				crtcId_ = crtc->crtc_id;
			}

			if (crtc)
			{
				screen_width_ = crtc->width;
				screen_height_ = crtc->height;
			}


				std::cerr << "Connector " << con->connector_id << " (crtc " << (crtc ? crtc->crtc_id : 0) << "): type "
				<< con->connector_type << ", " << (crtc ? crtc->width : 0) << "x" << (crtc ? crtc->height : 0)
				<< (conId_ == (int)con->connector_id ? " (chosen)" : "") << std::endl;
		}

		if (!conId_)
			throw std::runtime_error("No suitable enabled connector found");
	}

	crtcIdx_ = -1;

	for (i = 0; i < res->count_crtcs; ++i)
	{
		if (crtcId_ == res->crtcs[i])
		{
			crtcIdx_ = i;
			break;
		}
	}

	if (crtcIdx_ == -1)
	{
		drmModeFreeResources(res);
		throw std::runtime_error("drm: CRTC " + std::to_string(crtcId_) + " not found");
	}

	if (res->count_connectors <= 0)
	{
		drmModeFreeResources(res);
		throw std::runtime_error("drm: no connectors");
	}

	drmModeConnector* c;
	c = drmModeGetConnector(drmfd_, conId_);
	if (!c)
	{
		drmModeFreeResources(res);
		throw std::runtime_error("drmModeGetConnector failed: " + std::string(ERRSTR));
	}

	if (!c->count_modes)
	{
		drmModeFreeConnector(c);
		drmModeFreeResources(res);
		throw std::runtime_error("connector supports no mode");
	}

	drmModeCrtc* crtc = drmModeGetCrtc(drmfd_, crtcId_);
	x_ = crtc->x;
	y_ = crtc->y;
	width_ = crtc->width;
	height_ = crtc->height;
	drmModeFreeCrtc(crtc);
}

void DrmPreview::findPlane()
{
	drmModePlaneResPtr planes;
	drmModePlanePtr plane;
	unsigned int i;
	unsigned int j;

	planes = drmModeGetPlaneResources(drmfd_);
	if (!planes)
		throw std::runtime_error("drmModeGetPlaneResources failed: " + std::string(ERRSTR));

	try
	{
		for (i = 0; i < planes->count_planes; ++i)
		{
			plane = drmModeGetPlane(drmfd_, planes->planes[i]);
			if (!planes)
				throw std::runtime_error("drmModeGetPlane failed: " + std::string(ERRSTR));

			if (!(plane->possible_crtcs & (1 << crtcIdx_)))
			{
				drmModeFreePlane(plane);
				continue;
			}

			for (j = 0; j < plane->count_formats; ++j)
			{
				if (plane->formats[j] == out_fourcc_)
				{
					break;
				}
			}

			if (j == plane->count_formats)
			{
				drmModeFreePlane(plane);
				continue;
			}

			planeId_ = plane->plane_id;

			drmModeFreePlane(plane);
			break;
		}
	}
	catch (std::exception const& e)
	{
		drmModeFreePlaneResources(planes);
		throw;
	}

	drmModeFreePlaneResources(planes);
}

DrmPreview::DrmPreview() : last_fd_(-1), first_time_(true)
{
	drmfd_ = drmOpen("vc4", NULL);
	if (drmfd_ < 0)
		throw std::runtime_error("drmOpen failed: " + std::string(ERRSTR));

	x_ = 0;
	y_ = 0;
	width_ = 1280;
	height_ = 270;
	screen_width_ = 0;
	screen_height_ = 0;

	try
	{
		if (!drmIsMaster(drmfd_))
			throw std::runtime_error("DRM preview unavailable - not master");

		conId_ = 0;
		findCrtc();
		out_fourcc_ = DRM_FORMAT_YUV420;
		findPlane();
	}
	catch (std::exception const& e)
	{
		close(drmfd_);
		throw;
	}

	// Default behaviour here is to go fullscreen.


		x_ = y_ = 0;
		width_ = screen_width_;
		height_ = screen_height_;

}

// DRM doesn't seem to have userspace definitions of its enums, but the properties
// contain enum-name-to-value tables. So the code below ends up using strings and
// searching for name matches. I suppose it works...

static void get_colour_space_info(
	std::optional<libcamera::ColorSpace> const& cs,
	char const*& encoding,
	char const*& range)
{
	static char const encoding_601[] = "601", encoding_709[] = "709";
	static char const range_limited[] = "limited", range_full[] = "full";
	encoding = encoding_601;
	range = range_limited;

	if (cs == libcamera::ColorSpace::Jpeg)
		range = range_full;
	else if (cs == libcamera::ColorSpace::Smpte170m)
		/* all good */;
	else if (cs == libcamera::ColorSpace::Rec709)
		encoding = encoding_709;
	else
		std::cerr << "DrmPreview: unexpected colour space " << libcamera::ColorSpace::toString(cs) << std::endl;
}

static int drm_set_property(int fd, int plane_id, char const* name, char const* val)
{
	drmModeObjectPropertiesPtr properties = nullptr;
	drmModePropertyPtr prop = nullptr;
	int ret = -1;
	properties = drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);

	for (unsigned int i = 0; i < properties->count_props; i++)
	{
		int prop_id = properties->props[i];
		prop = drmModeGetProperty(fd, prop_id);
		if (!prop)
			continue;

		if (!drm_property_type_is(prop, DRM_MODE_PROP_ENUM) || !strstr(prop->name, name))
		{
			drmModeFreeProperty(prop);
			prop = nullptr;
			continue;
		}

		// We have found the right property from its name, now search the enum table
		// for the numerical value that corresponds to the value name that we have.
		for (int j = 0; j < prop->count_enums; j++)
		{
			if (!strstr(prop->enums[j].name, val))
				continue;

			ret = drmModeObjectSetProperty(fd, plane_id, DRM_MODE_OBJECT_PLANE, prop_id, prop->enums[j].value);
			if (ret < 0)
				std::cerr << "DrmPreview: failed to set value " << val << " for property " << name << std::endl;
			goto done;
		}

		std::cerr << "DrmPreview: failed to find value " << val << " for property " << name << std::endl;
		goto done;
	}

	std::cerr << "DrmPreview: failed to find property " << name << std::endl;
done:
	if (prop)
		drmModeFreeProperty(prop);
	if (properties)
		drmModeFreeObjectProperties(properties);
	return ret;
}

static void setup_colour_space(int fd, int plane_id, std::optional<libcamera::ColorSpace> const& cs)
{
	char const* encoding, * range;
	get_colour_space_info(cs, encoding, range);

	drm_set_property(fd, plane_id, "COLOR_ENCODING", encoding);
	drm_set_property(fd, plane_id, "COLOR_RANGE", range);
}

void DrmPreview::makeBuffer(int fd, size_t size, StreamInfo const& info, Buffer& buffer)
{
	if (first_time_)
	{
		first_time_ = false;

		setup_colour_space(drmfd_, planeId_, info.colour_space);
	}

	buffer.fd = fd;
	buffer.size = size;
	buffer.info = info;

	if (drmPrimeFDToHandle(drmfd_, fd, &buffer.bo_handle))
		throw std::runtime_error("drmPrimeFDToHandle failed for fd " + std::to_string(fd));

	uint32_t offsets[4] =
	{ 0, info.stride * info.height, info.stride * info.height + (info.stride / 2) * (info.height / 2) };
	uint32_t pitches[4] = { info.stride, info.stride / 2, info.stride / 2 };
	uint32_t bo_handles[4] = { buffer.bo_handle, buffer.bo_handle, buffer.bo_handle };

	if (drmModeAddFB2(drmfd_, info.width, info.height, out_fourcc_, bo_handles, pitches, offsets, &buffer.fb_handle, 0))
		throw std::runtime_error("drmModeAddFB2 failed: " + std::string(ERRSTR));
}

void DrmPreview::Show(int fd, libcamera::Span<uint8_t> span, StreamInfo const& info)
{
	Buffer& buffer = buffers_[fd];
	if (buffer.fd == -1)
		makeBuffer(fd, span.size(), info, buffer);

	unsigned int x_off = 0, y_off = 0;
	unsigned int w = width_, h = height_;
	if (info.width * height_ > width_ * info.height)
		h = width_ * info.height / info.width, y_off = (height_ - h) / 2;
	else
		w = height_ * info.width / info.height, x_off = (width_ - w) / 2;

	if (drmModeSetPlane(drmfd_, planeId_, crtcId_, buffer.fb_handle, 0, x_off + x_, y_off + y_, w, h, 0, 0,
		buffer.info.width << 16, buffer.info.height << 16))
		throw std::runtime_error("drmModeSetPlane failed: " + std::string(ERRSTR));
	if (last_fd_ >= 0)
		done_callback_(last_fd_);
	last_fd_ = fd;
}

void DrmPreview::Reset()
{
	for (auto& it : buffers_)
	{
		drmModeRmFB(drmfd_, it.second.fb_handle);
		// Apparently a "bo_handle" is a "gem" thing, and it needs closing. It feels like there
		// ought be an API to match "drmPrimeFDToHandle" for this, but I can only find an ioctl.
		drm_gem_close gem_close = {};
		gem_close.handle = it.second.bo_handle;
		if (drmIoctl(drmfd_, DRM_IOCTL_GEM_CLOSE, &gem_close) < 0)
			// I have no idea what this would mean, so complain and try to carry on...
			std::cerr << "DRM_IOCTL_GEM_CLOSE failed" << std::endl;
	}
	buffers_.clear();
	last_fd_ = -1;
	first_time_ = true;
}

DrmPreview* make_drm_preview()
{
	return new DrmPreview();
}