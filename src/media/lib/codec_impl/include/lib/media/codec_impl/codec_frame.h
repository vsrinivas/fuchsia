// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_FRAME_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_FRAME_H_

#include <lib/media/codec_impl/codec_vmo_range.h>

#include <cstdint>

class CodecBuffer;

// CodecFrame is used by some core codecs, regardless of whether there's a CodecBuffer or just a
// VMO.
// Move-only class
class CodecFrame {
 public:
  struct BufferSpec {
    uint64_t buffer_lifetime_ordinal = 0;
    uint32_t buffer_index = 0;
    CodecVmoRange vmo_range;
  };

  // Construct CodecFrame from an existing CodecBuffer.
  //
  // When using this constructor, the ownership
  // of the underlying VMO will remain with the CodecBuffer and the CodecBuffer must outlive the
  // CodecFrame.
  CodecFrame(const CodecBuffer& codec_buffer);

  // Construct CodecFrame from a BufferSpec.
  CodecFrame(BufferSpec buffer_spec);

  CodecFrame(CodecFrame&& from) = default;
  CodecFrame& operator=(CodecFrame&& from) = default;

  CodecFrame(const CodecFrame&) = delete;
  CodecFrame& operator=(const CodecFrame&) = delete;

  BufferSpec& buffer_spec() { return buffer_spec_; }
  const CodecBuffer* buffer_ptr() { return buffer_ptr_; }

 private:
  // When CodecBuffer is present, the BufferSpec does not own the VMO
  BufferSpec buffer_spec_;
  const CodecBuffer* buffer_ptr_ = nullptr;
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_FRAME_H_
