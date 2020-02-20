// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "video_frame.h"

#include <stdio.h>

// Normally this isn't allowed from a driver, but devmgr can be modified to
// allow it.
void DumpVideoFrameToFile(VideoFrame* frame, const char* filename) {
  FILE* f = fopen(filename, "a");
  io_buffer_cache_flush_invalidate(&frame->buffer, 0, frame->stride * frame->coded_height);
  io_buffer_cache_flush_invalidate(&frame->buffer, frame->uv_plane_offset,
                                   frame->stride * frame->coded_height / 2);

  uint8_t* buf_start = static_cast<uint8_t*>(io_buffer_virt(&frame->buffer));
  for (uint32_t y = 0; y < frame->coded_height; y++) {
    fwrite(buf_start + frame->stride * y, 1, frame->coded_width, f);
  }
  for (uint32_t y = 0; y < frame->coded_height / 2; y++) {
    fwrite(buf_start + frame->uv_plane_offset + frame->stride * y, 1, frame->coded_width, f);
  }
  fclose(f);
}
