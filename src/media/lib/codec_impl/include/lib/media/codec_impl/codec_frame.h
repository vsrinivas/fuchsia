// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEDIA_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_FRAME_H_
#define GARNET_LIB_MEDIA_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_FRAME_H_

#include <fuchsia/media/cpp/fidl.h>

class CodecBuffer;
// Move-only struct
struct CodecFrame {
  CodecFrame(CodecFrame&& from) = default;
  CodecFrame& operator=(CodecFrame&& from) = default;

  fuchsia::media::StreamBuffer codec_buffer_spec;
  const CodecBuffer* codec_buffer_ptr;
};

#endif  // GARNET_LIB_MEDIA_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_FRAME_H_
