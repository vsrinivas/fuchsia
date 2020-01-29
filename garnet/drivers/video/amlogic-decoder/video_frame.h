// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_FRAME_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_FRAME_H_

#include <cstdint>

#include <ddk/io-buffer.h>

class CodecBuffer;
struct VideoFrame {
  ~VideoFrame() { io_buffer_release(&buffer); }

  io_buffer_t buffer = {};
  uint32_t stride = 0;  // In bytes.

  // These can be odd when decoding VP9, and are preserved as reported from the
  // HW.  For h264 these are the same as coded_width and coded_height.
  uint32_t hw_width = 0;   // HW-reported width
  uint32_t hw_height = 0;  // HW-reported height

  // NV12 wants coded_width and coded_height to be even, so we round up the
  // hw_width and hw_height to ensure that coded_width and coded_height of the
  // NV12 output is even.  The display_width and display_height can still be
  // odd.
  uint32_t coded_width = 0;   // rounded-up coded_width for NV12
  uint32_t coded_height = 0;  // rounded-up coded_height for NV12

  uint32_t uv_plane_offset = 0;

  // These can be <= coded_width and coded_height respectively, and these can be
  // odd (for both h264 and VP9).
  uint32_t display_width = 0;
  uint32_t display_height = 0;

  // Index into the vector of decoded frames - for decoder use only.
  uint32_t index = 0xffffffff;
  bool has_pts = false;
  uint64_t pts = 0;

  const CodecBuffer* codec_buffer = nullptr;
};

// The video frame must be in NV12 format. The output file can be read using
// mplayer -demuxer rawvideo -rawvideo w=320:h=240:format=nv12
void DumpVideoFrameToFile(VideoFrame* frame, const char* filename);

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_FRAME_H_
