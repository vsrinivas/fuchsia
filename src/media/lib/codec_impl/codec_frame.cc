// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/media/codec_impl/codec_buffer.h>
#include <lib/media/codec_impl/codec_frame.h>

CodecFrame::CodecFrame(const CodecBuffer& codec_buffer)
    : buffer_spec_(
          BufferSpec{.buffer_lifetime_ordinal = codec_buffer.lifetime_ordinal(),
                     .buffer_index = codec_buffer.index(),
                     .vmo_range = CodecVmoRange(codec_buffer.vmo().borrow(),
                                                codec_buffer.vmo_offset(), codec_buffer.size())}),
      buffer_ptr_(&codec_buffer) {}

CodecFrame::CodecFrame(BufferSpec buffer_spec)
    : buffer_spec_(std::move(buffer_spec)), buffer_ptr_(nullptr) {}
