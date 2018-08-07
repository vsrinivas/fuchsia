// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_BUFFER_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_BUFFER_H_

#include "codec_port.h"

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fxl/macros.h>

#include <memory>

class CodecImpl;

// TODO(dustingreen): Support BufferCollection buffers.
//
// These are 1:1 with Codec buffers, but not necessarily 1:1 with core codec
// buffers.
//
// The const-ness of a CodecBuffer refers to the fields of the CodecBuffer
// instance, not to the data pointed at by buffer_base().
class CodecBuffer {
 public:
  uint64_t buffer_lifetime_ordinal() const;

  uint32_t buffer_index() const;

  uint8_t* buffer_base() const;

  size_t buffer_size() const;

  const fuchsia::mediacodec::CodecBuffer& codec_buffer() const;

 private:
  friend class CodecImpl;
  friend class std::unique_ptr<CodecBuffer>;
  friend struct std::default_delete<CodecBuffer>;

  CodecBuffer(CodecImpl* parent, CodecPort port,
              fuchsia::mediacodec::CodecBuffer buffer);
  ~CodecBuffer();
  bool Init(bool input_require_write = false);

  // The parent CodecImpl instance.  Just so we can call parent_->Fail().
  // The parent_ CodecImpl out-lives the CodecImpl::Buffer.
  CodecImpl* parent_;

  CodecPort port_ = kFirstPort;

  // This msg still has the live vmo_handle.
  fuchsia::mediacodec::CodecBuffer buffer_;

  // This accounts for vmo_offset_begin.  The content bytes are not part of
  // a Buffer instance from a const-ness point of view.
  uint8_t* buffer_base_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(CodecBuffer);
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_BUFFER_H_
