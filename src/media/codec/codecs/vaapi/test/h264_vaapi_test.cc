// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <gtest/gtest.h>
#include <va/va.h>

#include "src/media/codec/codecs/vaapi/vaapi_utils.h"

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

static int global_display_ptr;

VADisplay vaGetDisplayMagma(magma_device_t device) { return &global_display_ptr; }

namespace {

TEST(H264Vaapi, CodecList) {
  EXPECT_TRUE(VADisplayWrapper::InitializeSingletonForTesting());
  auto codec_list = GetCodecList();
  EXPECT_EQ(2u, codec_list.size());
}

}  // namespace
