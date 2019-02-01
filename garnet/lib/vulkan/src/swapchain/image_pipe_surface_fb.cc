// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "image_pipe_surface_fb.h"
#include <cstdio>
#include "lib/framebuffer/framebuffer.h"

namespace image_pipe_swapchain {

ImagePipeSurfaceFb::ImagePipeSurfaceFb() {
  const char* err = nullptr;
  zx_status_t status = fb_bind(false, &err);
  if (status != ZX_OK) {
    fprintf(stderr, "fb_bind failed: %d (%s)\n", status, err);
  }
}

ImagePipeSurfaceFb::~ImagePipeSurfaceFb() { fb_release(); }

bool ImagePipeSurfaceFb::GetSize(uint32_t* width_out, uint32_t* height_out) {
  uint32_t stride;
  zx_pixel_format_t format;
  fb_get_config(width_out, height_out, &stride, &format);
  return true;
}

void ImagePipeSurfaceFb::AddImage(uint32_t image_id,
                                  fuchsia::images::ImageInfo image_info,
                                  zx::vmo buffer, uint64_t size_bytes) {
  // Must be consistent with intel-gpu-core.h and the tiling format
  // used by VK_IMAGE_USAGE_SCANOUT_BIT_GOOGLE.
  const uint32_t kImageTypeXTiled = 1;
  uint64_t fb_image_id = FB_INVALID_ID;
  zx_status_t status =
      fb_import_image(buffer.release(), kImageTypeXTiled, &fb_image_id);
  if (status != ZX_OK) {
    fprintf(stderr, "fb_import_image failed: %d\n", status);
    return;
  }

  image_id_map[image_id] = fb_image_id;
}

void ImagePipeSurfaceFb::RemoveImage(uint32_t image_id) {
  auto iter = image_id_map.find(image_id);
  if (iter != image_id_map.end()) {
    fb_release_image(iter->second);
    image_id_map.erase(iter);
  }
}

void ImagePipeSurfaceFb::PresentImage(
    uint32_t image_id, std::vector<zx::event> wait_events,
    std::vector<zx::event> signal_events) {

  assert(wait_events.size() <= 1);
  assert(signal_events.size() <= 1);

  auto iter = image_id_map.find(image_id);
  if (iter == image_id_map.end()) {
    fprintf(stderr, "PresentImage: can't find image_id %u\n", image_id);
    return;
  }

  uint64_t fb_image_id = iter->second;

  uint64_t wait_event_id = FB_INVALID_ID;
  if (wait_events.size()) {
    zx_info_handle_basic_t info;
    zx::event event = std::move(wait_events[0]);
    zx_status_t status = event.get_info(ZX_INFO_HANDLE_BASIC, &info,
                                        sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
      fprintf(stderr, "failed to get event id: %d\n", status);
      return;
    }
    wait_event_id = info.koid;
    status = fb_import_event(event.release(), wait_event_id);
    if (status != ZX_OK) {
      fprintf(stderr, "fb_import_event failed: %d\n", status);
      return;
    }
  }

  uint64_t signal_event_id = FB_INVALID_ID;
  if (signal_events.size()) {
    zx_info_handle_basic_t info;
    zx::event event = std::move(signal_events[0]);
    zx_status_t status = event.get_info(ZX_INFO_HANDLE_BASIC, &info,
                                        sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
      fprintf(stderr, "failed to get event id: %d\n", status);
      return;
    }
    signal_event_id = info.koid;
    status = fb_import_event(event.release(), signal_event_id);
    if (status != ZX_OK) {
      fprintf(stderr, "fb_import_event failed: %d\n", status);
      return;
    }
  }

  zx_status_t status =
      fb_present_image2(fb_image_id, wait_event_id, signal_event_id);

  if (wait_event_id != FB_INVALID_ID) {
    fb_release_event(wait_event_id);
  }
  if (signal_event_id != FB_INVALID_ID) {
    fb_release_event(signal_event_id);
  }

  if (status != ZX_OK) {
    fprintf(stderr, "fb_present_image2 failed: %d\n", status);
    return;
  }
}

}  // namespace image_pipe_swapchain
