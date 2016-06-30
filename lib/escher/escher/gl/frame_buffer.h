// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/base/macros.h"
#include "escher/geometry/size_i.h"
#include "escher/gl/unique_frame_buffer.h"
#include "escher/gl/unique_texture.h"

namespace escher {

class FrameBuffer {
 public:
  // The mask argument understands GL_COLOR_BUFFER_BIT and GL_DEPTH_BUFFER_BIT.
  // TODO(abarth): Support stencil buffers.
  explicit FrameBuffer(GLbitfield mask);
  ~FrameBuffer();

  SizeI size() const { return size_; }
  bool SetSize(const SizeI& size);

  const UniqueFrameBuffer& frame_buffer() const { return frame_buffer_; };
  const UniqueTexture& depth() const { return depth_; };
  const UniqueTexture& color() const { return color_; };

  // Returns the old color texture (if any).
  // Requires that the frame buffer was constructed with GL_COLOR_BUFFER_BIT.
  UniqueTexture SetColorTexture(UniqueTexture color);

 private:
  bool CheckStatusIfDebug();

  bool has_depth_;
  bool has_color_;

  UniqueFrameBuffer frame_buffer_;
  UniqueTexture depth_;
  UniqueTexture color_;
  SizeI size_;

  ESCHER_DISALLOW_COPY_AND_ASSIGN(FrameBuffer);
};

}  // namespace escher
