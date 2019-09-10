// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/framebuffer/framebuffer.h"

#include <fcntl.h>
#include <zircon/pixelformat.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

TEST(Framebuffer, SingleBuffer) {
  fbl::unique_fd dc_fd(open("/dev/class/display-controller/000", O_RDWR));
  if (!dc_fd) {
    fprintf(stdout, "Skipping test because of no display controller\n");
    return;
  }
  dc_fd.reset();
  constexpr uint32_t kIterations = 2;

  for (uint32_t i = 0; i < kIterations; i++) {
    const char* error;
    zx_status_t status = fb_bind(true, &error);
    if (status == ZX_ERR_NO_RESOURCES) {
      // If the simple display driver is being used then only one client can
      // connect to the display at a time. virtcon is probably already using it,
      // so libframebuffer isn't supported there.
      fprintf(stderr, "Skipping because received ZX_ERR_NO_RESOURCES\n");
      return;
    }
    EXPECT_OK(status);
    zx_handle_t buffer_handle = fb_get_single_buffer();
    EXPECT_NE(ZX_HANDLE_INVALID, buffer_handle);

    uint32_t width, height, linear_stride_px;
    zx_pixel_format_t format;
    fb_get_config(&width, &height, &linear_stride_px, &format);
    EXPECT_LE(width, linear_stride_px);
    EXPECT_LT(0u, ZX_PIXEL_FORMAT_BYTES(format));

    uint64_t buffer_size;
    EXPECT_OK(zx_vmo_get_size(buffer_handle, &buffer_size));
    EXPECT_LE(linear_stride_px * ZX_PIXEL_FORMAT_BYTES(format) * height, buffer_size);

    fb_release();
  }
}
