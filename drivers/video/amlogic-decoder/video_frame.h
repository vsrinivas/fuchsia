// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_FRAME_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_FRAME_H_

#include <ddk/io-buffer.h>

#include <cstdint>

class CodecPacket;
struct VideoFrame {
  ~VideoFrame() { io_buffer_release(&buffer); }

  io_buffer_t buffer = {};
  uint32_t stride = 0;  // In bytes.
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t uv_plane_offset = 0;
  uint32_t display_width = 0;
  uint32_t display_height = 0;
  // Index into the vector of decoded frames - for decoder use only.
  uint32_t index = 0xffffffff;
  bool has_pts = false;
  uint64_t pts = 0;

  CodecPacket* codec_packet = nullptr;
};

// The video frame must be in NV12 format. The output file can be read using
// mplayer -demuxer rawvideo -rawvideo w=320:h=240:format=nv12
void DumpVideoFrameToFile(VideoFrame* frame, const char* filename);

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_FRAME_H_
