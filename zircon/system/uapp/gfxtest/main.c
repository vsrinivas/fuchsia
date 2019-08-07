// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <lib/framebuffer/framebuffer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <gfx/gfx.h>

int main(int argc, char* argv[]) {
  const char* err;
  zx_status_t status = fb_bind(true, &err);
  if (status != ZX_OK) {
    printf("failed to open framebuffer: %d (%s)\n", status, err);
    return -1;
  }
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  zx_pixel_format_t format;

  fb_get_config(&width, &height, &stride, &format);

  size_t size = stride * ZX_PIXEL_FORMAT_BYTES(format) * height;
  uintptr_t fbo;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0,
                       fb_get_single_buffer(), 0, size, &fbo);
  if (status < 0) {
    printf("failed to map fb (%d)\n", status);
    return -1;
  }

  gfx_surface* gfx =
      gfx_create_surface((void*)fbo, width, height, stride, format, GFX_FLAG_FLUSH_CPU_CACHE);
  if (!gfx) {
    printf("failed to create gfx surface\n");
    return -1;
  }
  gfx_fillrect(gfx, 0, 0, gfx->width, gfx->height, 0xffffffff);
  gfx_flush(gfx);

  int d = gfx->height / 5;
  int i = 10;
  while (i--) {
    zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
    gfx_fillrect(gfx, (gfx->width - d) / 2, (gfx->height - d) / 2, d, d,
                 i % 2 ? 0xff55ff55 : 0xffaa00aa);
    gfx_flush(gfx);
  }

  gfx_surface_destroy(gfx);
  fb_release();
}
