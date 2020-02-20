// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "video_frame_helpers.h"

#include <cstdio>

void HashFrame(VideoFrame* frame, uint8_t digest[SHA256_DIGEST_LENGTH]) {
  io_buffer_cache_flush_invalidate(&frame->buffer, 0, frame->stride * frame->coded_height);
  io_buffer_cache_flush_invalidate(&frame->buffer, frame->uv_plane_offset,
                                   frame->stride * frame->coded_height / 2);

  uint8_t* buf_start = static_cast<uint8_t*>(io_buffer_virt(&frame->buffer));
  SHA256_CTX sha256_ctx;
  SHA256_Init(&sha256_ctx);
  // NV12 Y plane
  for (uint32_t y = 0; y < frame->coded_height; y++) {
    SHA256_Update(&sha256_ctx, buf_start + frame->stride * y, frame->coded_width);
  }
  // uv plane
  for (uint32_t y = 0; y < frame->coded_height / 2; y++) {
    SHA256_Update(&sha256_ctx, buf_start + frame->uv_plane_offset + frame->stride * y,
                  frame->coded_width);
  }
  SHA256_Final(digest, &sha256_ctx);
}

std::string StringifyHash(uint8_t digest[SHA256_DIGEST_LENGTH]) {
  char actual_sha256[SHA256_DIGEST_LENGTH * 2 + 1];
  char* actual_sha256_ptr = actual_sha256;
  for (uint32_t i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
    // Writes the terminating 0 each time, returns 2 each time.
    actual_sha256_ptr += snprintf(actual_sha256_ptr, 3, "%02x", digest[i]);
  }
  return std::string(actual_sha256);
}
