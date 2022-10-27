// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>

#include <gtest/gtest.h>
#include <va/va.h>
#include <va/va_magma.h>

static VAStatus vaCreateConfigReturn = VA_STATUS_SUCCESS;
static VAStatus vaCreateContextReturn = VA_STATUS_SUCCESS;
static VAStatus vaCreateSurfacesReturn = VA_STATUS_SUCCESS;
static int vaGetDisplayMagmaReturn;

static const std::set<VASurfaceID> vaFreeSurfacesDefault = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
    44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63};
static std::set<VASurfaceID> vaFreeSurfaces = vaFreeSurfacesDefault;

void vaDefaultStubSetReturn() {
  vaCreateConfigReturn = VA_STATUS_SUCCESS;
  vaCreateContextReturn = VA_STATUS_SUCCESS;
  vaCreateSurfacesReturn = VA_STATUS_SUCCESS;
  vaFreeSurfaces = vaFreeSurfacesDefault;
}

struct FakeBuffer {
  VABufferType type{};
  size_t size{};
  std::unique_ptr<std::vector<uint8_t>> mapped_buffer;
  std::unique_ptr<VACodedBufferSegment> coded_segment;
};

static std::map<VABufferID, FakeBuffer> fake_buffer_map_;
static VABufferID next_buffer_id_;

void vaCreateConfigStubSetReturn(VAStatus status) { vaCreateConfigReturn = status; }

void vaCreateContextStubSetReturn(VAStatus status) { vaCreateContextReturn = status; }

void vaCreateSurfacesStubSetReturn(VAStatus status) { vaCreateSurfacesReturn = status; }

int vaMaxNumEntrypoints(VADisplay dpy) { return 2; }
VAStatus vaQueryConfigEntrypoints(VADisplay dpy, VAProfile profile, VAEntrypoint *entrypoint_list,
                                  int *num_entrypoints) {
  entrypoint_list[0] = VAEntrypointVLD;
  entrypoint_list[1] = VAEntrypointEncSliceLP;
  *num_entrypoints = 2;
  return VA_STATUS_SUCCESS;
}
VAStatus vaGetConfigAttributes(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint,
                               VAConfigAttrib *attrib_list, int num_attribs) {
  EXPECT_EQ(1, num_attribs);
  uint32_t &value = attrib_list[0].value;
  switch (attrib_list[0].type) {
    case VAConfigAttribRTFormat:
      value = VA_RT_FORMAT_YUV420;
      break;
    case VAConfigAttribEncPackedHeaders:
      value =
          VA_ENC_PACKED_HEADER_SEQUENCE | VA_ENC_PACKED_HEADER_PICTURE | VA_ENC_PACKED_HEADER_SLICE;
      break;
    case VAConfigAttribEncMaxRefFrames:
      value = 1;
      break;
    default:
      EXPECT_TRUE(false) << "Unexpected attrib type " << attrib_list[0].type;
      break;
  }
  return VA_STATUS_SUCCESS;
}
VAStatus vaDestroyConfig(VADisplay dpy, VAConfigID config_id) {
  return VA_STATUS_ERROR_FLAG_NOT_SUPPORTED;
}
VAStatus vaCreateConfig(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint,
                        VAConfigAttrib *attrib_list, int num_attribs, VAConfigID *config_id) {
  *config_id = 1;
  return vaCreateConfigReturn;
}
int vaMaxNumConfigAttributes(VADisplay dpy) { return 6; }
VAStatus vaQueryConfigAttributes(VADisplay dpy, VAConfigID config_id, VAProfile *profile,
                                 VAEntrypoint *entrypoint, VAConfigAttrib *attrib_list,
                                 int *num_attribs) {
  attrib_list[0].type = VAConfigAttribMaxPictureHeight;
  attrib_list[0].value = 3840;
  attrib_list[1].type = VAConfigAttribMaxPictureWidth;
  attrib_list[1].value = 2160;
  *num_attribs = 2;
  return VA_STATUS_SUCCESS;
}
VAStatus vaCreateSurfaces(VADisplay dpy, unsigned int format, unsigned int width,
                          unsigned int height, VASurfaceID *surfaces, unsigned int num_surfaces,
                          VASurfaceAttrib *attrib_list, unsigned int num_attribs) {
  // User set return values take precedent
  if (vaCreateSurfacesReturn != VA_STATUS_SUCCESS) {
    return vaCreateSurfacesReturn;
  }

  if (vaFreeSurfaces.size() < num_surfaces) {
    return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
  }

  for (size_t i = 0; i < num_surfaces; i++) {
    surfaces[i] = *vaFreeSurfaces.begin();
    vaFreeSurfaces.erase(vaFreeSurfaces.begin());
  }

  return VA_STATUS_SUCCESS;
}
VAStatus vaDestroySurfaces(VADisplay dpy, VASurfaceID *surfaces, int num_surfaces) {
  for (int surface_idx = 0; surface_idx < num_surfaces; surface_idx += 1) {
    if (vaFreeSurfaces.count(surfaces[surface_idx]) != 0) {
      return VA_STATUS_ERROR_INVALID_SURFACE;
    }
  }

  for (int surface_idx = 0; surface_idx < num_surfaces; surface_idx += 1) {
    vaFreeSurfaces.insert(surfaces[surface_idx]);
  }

  return VA_STATUS_SUCCESS;
}
VAStatus vaCreateContext(VADisplay dpy, VAConfigID config_id, int picture_width, int picture_height,
                         int flag, VASurfaceID *render_targets, int num_render_targets,
                         VAContextID *context) {
  *context = 1;
  return vaCreateContextReturn;
}
VAStatus vaDestroyContext(VADisplay dpy, VAContextID context) { return VA_STATUS_SUCCESS; }
VAStatus vaBeginPicture(VADisplay dpy, VAContextID context, VASurfaceID render_target) {
  return VA_STATUS_SUCCESS;
}
VAStatus vaRenderPicture(VADisplay dpy, VAContextID context, VABufferID *buffers, int num_buffers) {
  return VA_STATUS_SUCCESS;
}
VAStatus vaEndPicture(VADisplay dpy, VAContextID context) { return VA_STATUS_SUCCESS; }
VAStatus vaSyncSurface(VADisplay dpy, VASurfaceID render_target) { return VA_STATUS_SUCCESS; }
// If the vaSyncSurface stub ever returns VA_STATUS_ERROR_DECODING_ERROR, this stub should be
// updated since the client will query why vaSyncSurface failed
VAStatus vaQuerySurfaceError(VADisplay dpy, VASurfaceID surface, VAStatus error_status,
                             void **error_info) {
  return VA_STATUS_SUCCESS;
}
VAStatus vaGetImage(VADisplay dpy, VASurfaceID surface, int x, int y, unsigned int width,
                    unsigned int height, VAImageID image) {
  return VA_STATUS_SUCCESS;
}

VAStatus vaDeriveImage(VADisplay dpy, VASurfaceID surface, VAImage *image) {
  // Arbitrary dimensions that match those in the H264 Encoder tests.
  constexpr uint32_t kImageBufferSize = 12 * 12 * 3 / 2;
  fake_buffer_map_[next_buffer_id_].size = kImageBufferSize;
  image->buf = next_buffer_id_++;
  image->offsets[0] = 0;
  image->pitches[0] = 10;
  image->offsets[1] = 10 * 10;
  image->pitches[1] = 10;

  return VA_STATUS_SUCCESS;
}

VAStatus vaDestroyImage(VADisplay dpy, VAImageID image) { return VA_STATUS_SUCCESS; }

VAStatus vaCreateBuffer(VADisplay dpy, VAContextID context, VABufferType type, unsigned int size,
                        unsigned int num_elements, void *data, VABufferID *buf_id) {
  FakeBuffer &fake_buffer = fake_buffer_map_[next_buffer_id_];
  fake_buffer.size = size;
  fake_buffer.type = type;
  *buf_id = next_buffer_id_++;

  return VA_STATUS_SUCCESS;
}
VAStatus vaDestroyBuffer(VADisplay dpy, VABufferID buffer_id) {
  fake_buffer_map_.erase(buffer_id);
  return VA_STATUS_SUCCESS;
}

VAStatus vaInitialize(VADisplay dpy, int *major_version, int *minor_version) {
  *major_version = VA_MAJOR_VERSION;
  *minor_version = VA_MINOR_VERSION;
  return VA_STATUS_SUCCESS;
}

VAStatus vaMapBuffer(VADisplay dpy, VABufferID buf_id, void **pbuf) {
  FakeBuffer &buf = fake_buffer_map_[buf_id];
  buf.mapped_buffer = std::make_unique<std::vector<uint8_t>>(buf.size);

  if (buf.type == VAEncCodedBufferType) {
    buf.coded_segment = std::make_unique<VACodedBufferSegment>();
    *pbuf = buf.coded_segment.get();
    buf.coded_segment->size = 10;
    buf.coded_segment->buf = buf.mapped_buffer->data();
    buf.coded_segment->next = nullptr;
  } else {
    *pbuf = buf.mapped_buffer->data();
  }

  return VA_STATUS_SUCCESS;
}

VAStatus vaUnmapBuffer(VADisplay dpy, VABufferID buf_id) { return VA_STATUS_SUCCESS; }

const char *vaErrorStr(VAStatus error_status) {
  switch (error_status) {
    case VA_STATUS_SUCCESS:
      return "success (no error)";
    case VA_STATUS_ERROR_OPERATION_FAILED:
      return "operation failed";
    case VA_STATUS_ERROR_ALLOCATION_FAILED:
      return "resource allocation failed";
    case VA_STATUS_ERROR_INVALID_DISPLAY:
      return "invalid VADisplay";
    case VA_STATUS_ERROR_INVALID_CONFIG:
      return "invalid VAConfigID";
    case VA_STATUS_ERROR_INVALID_CONTEXT:
      return "invalid VAContextID";
    case VA_STATUS_ERROR_INVALID_SURFACE:
      return "invalid VASurfaceID";
    case VA_STATUS_ERROR_INVALID_BUFFER:
      return "invalid VABufferID";
    case VA_STATUS_ERROR_INVALID_IMAGE:
      return "invalid VAImageID";
    case VA_STATUS_ERROR_INVALID_SUBPICTURE:
      return "invalid VASubpictureID";
    case VA_STATUS_ERROR_ATTR_NOT_SUPPORTED:
      return "attribute not supported";
    case VA_STATUS_ERROR_MAX_NUM_EXCEEDED:
      return "list argument exceeds maximum number";
    case VA_STATUS_ERROR_UNSUPPORTED_PROFILE:
      return "the requested VAProfile is not supported";
    case VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT:
      return "the requested VAEntryPoint is not supported";
    case VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT:
      return "the requested RT Format is not supported";
    case VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE:
      return "the requested VABufferType is not supported";
    case VA_STATUS_ERROR_SURFACE_BUSY:
      return "surface is in use";
    case VA_STATUS_ERROR_FLAG_NOT_SUPPORTED:
      return "flag not supported";
    case VA_STATUS_ERROR_INVALID_PARAMETER:
      return "invalid parameter";
    case VA_STATUS_ERROR_RESOLUTION_NOT_SUPPORTED:
      return "resolution not supported";
    case VA_STATUS_ERROR_UNIMPLEMENTED:
      return "the requested function is not implemented";
    case VA_STATUS_ERROR_SURFACE_IN_DISPLAYING:
      return "surface is in displaying (may by overlay)";
    case VA_STATUS_ERROR_INVALID_IMAGE_FORMAT:
      return "invalid VAImageFormat";
    case VA_STATUS_ERROR_DECODING_ERROR:
      return "internal decoding error";
    case VA_STATUS_ERROR_ENCODING_ERROR:
      return "internal encoding error";
    case VA_STATUS_ERROR_INVALID_VALUE:
      return "an invalid/unsupported value was supplied";
    case VA_STATUS_ERROR_UNSUPPORTED_FILTER:
      return "the requested filter is not supported";
    case VA_STATUS_ERROR_INVALID_FILTER_CHAIN:
      return "an invalid filter chain was supplied";
    case VA_STATUS_ERROR_HW_BUSY:
      return "HW busy now";
    case VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE:
      return "an unsupported memory type was supplied";
    case VA_STATUS_ERROR_NOT_ENOUGH_BUFFER:
      return "allocated memory size is not enough for input or output";
  }
  return "unknown libva error / description missing";
}

VADisplay vaGetDisplayMagma(magma_device_t device) { return &vaGetDisplayMagmaReturn; }

VAMessageCallback vaSetErrorCallback(VADisplay dpy, VAMessageCallback callback,
                                     void *user_context) {
  return nullptr;
}

VAMessageCallback vaSetInfoCallback(VADisplay dpy, VAMessageCallback callback, void *user_context) {
  return nullptr;
}
