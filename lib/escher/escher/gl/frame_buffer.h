// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "ftl/macros.h"
#include "escher/geometry/size_i.h"
#include "escher/gl/unique_frame_buffer.h"
#include "escher/gl/texture.h"
#include "escher/gl/texture_cache.h"

namespace escher {

class FrameBuffer {
 public:
  FrameBuffer();
  explicit FrameBuffer(UniqueFrameBuffer frame_buffer);
  ~FrameBuffer();

  FrameBuffer(FrameBuffer&& other);
  FrameBuffer& operator=(FrameBuffer&& other);

  static FrameBuffer Make();

  explicit operator bool() const { return static_cast<bool>(frame_buffer_); }

  void Bind();

  const UniqueFrameBuffer& frame_buffer() const { return frame_buffer_; }
  const Texture& depth() const { return depth_; };
  const Texture& color() const { return color_; };

  void SetDepth(Texture depth);
  void SetColor(Texture color);

  Texture TakeColor();
  Texture SwapColor(Texture color);

 private:
  bool CheckStatusIfDebug();
  bool IsBound();

  UniqueFrameBuffer frame_buffer_;
  Texture depth_;
  Texture color_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FrameBuffer);
};

}  // namespace escher
