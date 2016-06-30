// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/gl/frame_buffer.h"

#include <iostream>

namespace escher {

FrameBuffer::FrameBuffer(GLbitfield mask)
    : has_depth_(mask & GL_DEPTH_BUFFER_BIT),
      has_color_(mask & GL_COLOR_BUFFER_BIT) {}

FrameBuffer::~FrameBuffer() {}

bool FrameBuffer::SetSize(const SizeI& size) {
  if (size_.Equals(size))
    return !!frame_buffer_;
  size_ = size;

  if (!frame_buffer_)
    frame_buffer_ = MakeUniqueFrameBuffer();
  ESCHER_DCHECK(frame_buffer_);

  glBindFramebuffer(GL_FRAMEBUFFER, frame_buffer_.id());

  if (has_depth_) {
    depth_ = MakeDepthTexture(size);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           depth_.id(), 0);
  }

  if (has_color_) {
    color_ = MakeColorTexture(size_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           color_.id(), 0);
  }

  return CheckStatusIfDebug();
}

UniqueTexture FrameBuffer::SetColorTexture(UniqueTexture color) {
  ESCHER_DCHECK(has_color_);
  ESCHER_DCHECK(frame_buffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, frame_buffer_.id());
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         color.id(), 0);
  ESCHER_DCHECK(CheckStatusIfDebug());
  std::swap(color_, color);
  return std::move(color);
}

bool FrameBuffer::CheckStatusIfDebug() {
#ifndef NDEBUG
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "error: frame buffer: " << frame_buffer_.id() << ": 0x"
              << std::hex << status << std::endl;
    return false;
  }
  return true;
#else
  return true;
#endif
}

}  // namespace escher
