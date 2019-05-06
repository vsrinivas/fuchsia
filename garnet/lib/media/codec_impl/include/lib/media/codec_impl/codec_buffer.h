// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEDIA_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_BUFFER_H_
#define GARNET_LIB_MEDIA_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_BUFFER_H_

#include <fbl/macros.h>
#include <fuchsia/media/cpp/fidl.h>

#include <memory>

#include "codec_port.h"

class CodecImpl;

// Core codec representation of a video frame.  Different core codecs may have
// very different implementations of this.
//
// TODO(dustingreen): Have this be a base class that's defined by the
// CodecImpl source_set, and have amlogic-video VideoFrame derive from that base
// class.
//
// Regardless of codec, these will be managed by shared_ptr<>, because for
// decoder reference frames, shared_ptr<> makes sense.
struct VideoFrame;

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

  const fuchsia::media::StreamBuffer& codec_buffer() const;

  // The use of weak_ptr<> here is to emphasize that we don't need shared_ptr<>
  // to keep the VideoFrame(s) alive.  We'd use a raw pointer here if it weren't
  // for needing to convert to a shared_ptr<> to call certain methods that
  // expect shared_ptr<>.
  //
  // This is marked const because it only mutates a mutable field, which is
  // considered mutable because it's about establishing an association between
  // video_frame and CodecBuffer after CodecBuffer has been constructed.
  void SetVideoFrame(std::weak_ptr<VideoFrame> video_frame) const;
  std::weak_ptr<VideoFrame> video_frame() const;

 private:
  friend class CodecImpl;
  friend class std::unique_ptr<CodecBuffer>;
  friend struct std::default_delete<CodecBuffer>;

  CodecBuffer(CodecImpl* parent, CodecPort port,
              fuchsia::media::StreamBuffer buffer);
  ~CodecBuffer();
  bool Init(bool input_require_write = false);

  // The parent CodecImpl instance.  Just so we can call parent_->Fail().
  // The parent_ CodecImpl out-lives the CodecImpl::Buffer.
  CodecImpl* parent_;

  CodecPort port_ = kFirstPort;

  // This msg still has the live vmo_handle.
  fuchsia::media::StreamBuffer buffer_;

  // Mutable only in the sense that it's set later than the constructor.  The
  // association does not switch to a different VideoFrame once set.
  mutable std::weak_ptr<VideoFrame> video_frame_;

  // This accounts for vmo_offset_begin.  The content bytes are not part of
  // a Buffer instance from a const-ness point of view.
  uint8_t* buffer_base_ = nullptr;

  DISALLOW_COPY_ASSIGN_AND_MOVE(CodecBuffer);
};

#endif  // GARNET_LIB_MEDIA_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_BUFFER_H_
