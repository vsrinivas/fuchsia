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

  // This count _may_ be used to indicate to some core codecs which CodecFrame(s) are initially
  // free vs. initially used (and how many times) until they become free for the first time when
  // "returned" enough times later (without the core codec instance ever having indicated the frame
  // as being output; the logical frame became used when output by a previous core codec instance,
  // or is simply not being provided to the core codec just yet).  Not all core codecs use this
  // field.
  uint32_t& initial_usage_count() { return initial_usage_count_; }

 private:
  // When CodecBuffer is present, the BufferSpec does not own the VMO
  BufferSpec buffer_spec_;
  const CodecBuffer* buffer_ptr_ = nullptr;
  // For core codecs that don't use initial_usage_count(), the default of 0 will be ignored; the
  // effect is the same as the default of 0 in that the frame will be considered free.
  uint32_t initial_usage_count_ = 0;
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_FRAME_H_
