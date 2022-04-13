// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <va/va.h>

int vaMaxNumEntrypoints(VADisplay dpy) { return 2; }
VAStatus vaQueryConfigEntrypoints(VADisplay dpy, VAProfile profile, VAEntrypoint *entrypoint_list,
                                  int *num_entrypoints) {
  entrypoint_list[0] = VAEntrypointVLD;
  *num_entrypoints = 1;
  return VA_STATUS_SUCCESS;
}
VAStatus vaGetConfigAttributes(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint,
                               VAConfigAttrib *attrib_list, int num_attribs) {
  EXPECT_EQ(1, num_attribs);
  EXPECT_EQ(VAConfigAttribRTFormat, attrib_list[0].type);
  attrib_list[0].value = VA_RT_FORMAT_YUV420;
  return VA_STATUS_SUCCESS;
}
VAStatus vaDestroyConfig(VADisplay dpy, VAConfigID config_id) {
  return VA_STATUS_ERROR_FLAG_NOT_SUPPORTED;
}
VAStatus vaCreateConfig(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint,
                        VAConfigAttrib *attrib_list, int num_attribs, VAConfigID *config_id) {
  *config_id = 1;
  return VA_STATUS_SUCCESS;
}
VAStatus vaQueryConfigAttributes(VADisplay dpy, VAConfigID config_id, VAProfile *profile,
                                 VAEntrypoint *entrypoint, VAConfigAttrib *attrib_list,
                                 int *num_attribs) {
  return VA_STATUS_ERROR_FLAG_NOT_SUPPORTED;
}
VAStatus vaCreateSurfaces(VADisplay dpy, unsigned int format, unsigned int width,
                          unsigned int height, VASurfaceID *surfaces, unsigned int num_surfaces,
                          VASurfaceAttrib *attrib_list, unsigned int num_attribs) {
  for (size_t i = 0; i < num_surfaces; i++) {
    surfaces[i] = static_cast<unsigned int>(i + 1);
  }
  return VA_STATUS_SUCCESS;
}
VAStatus vaDestroySurfaces(VADisplay dpy, VASurfaceID *surfaces, int num_surfaces) {
  return VA_STATUS_SUCCESS;
}
VAStatus vaCreateContext(VADisplay dpy, VAConfigID config_id, int picture_width, int picture_height,
                         int flag, VASurfaceID *render_targets, int num_render_targets,
                         VAContextID *context) {
  *context = 1;
  return VA_STATUS_SUCCESS;
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
VAStatus vaGetImage(VADisplay dpy, VASurfaceID surface, int x, int y, unsigned int width,
                    unsigned int height, VAImageID image) {
  return VA_STATUS_SUCCESS;
}
VAStatus vaDeriveImage(VADisplay dpy, VASurfaceID surface, VAImage *image) {
  return VA_STATUS_SUCCESS;
}
VAStatus vaDestroyImage(VADisplay dpy, VAImageID image) { return VA_STATUS_SUCCESS; }
VAStatus vaCreateBuffer(VADisplay dpy, VAContextID context, VABufferType type, unsigned int size,
                        unsigned int num_elements, void *data, VABufferID *buf_id) {
  return VA_STATUS_SUCCESS;
}
VAStatus vaDestroyBuffer(VADisplay dpy, VABufferID buffer_id) { return VA_STATUS_SUCCESS; }
VAStatus vaInitialize(VADisplay dpy, int *major_version, int *minor_version) {
  *major_version = VA_MAJOR_VERSION;
  *minor_version = VA_MINOR_VERSION;
  return VA_STATUS_SUCCESS;
}
VAStatus vaMapBuffer(VADisplay dpy, VABufferID buf_id, void **pbuf) { return VA_STATUS_SUCCESS; }
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
