// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_FRAME_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_FRAME_H_

#include <fuchsia/mediacodec/cpp/fidl.h>

class CodecPacket;
// Move-only struct
struct CodecFrame {
  CodecFrame(CodecFrame&& from) = default;
  CodecFrame& operator=(CodecFrame&& from) = default;

  fuchsia::mediacodec::CodecBuffer codec_buffer;
  CodecPacket* codec_packet;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_FRAME_H_
