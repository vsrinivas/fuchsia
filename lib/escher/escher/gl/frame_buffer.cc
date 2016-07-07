// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/gl/frame_buffer.h"

#include <iostream>

#include "ftl/logging.h"

namespace escher {

FrameBuffer::FrameBuffer() {}

FrameBuffer::FrameBuffer(UniqueFrameBuffer frame_buffer)
  : frame_buffer_(std::move(frame_buffer)) {}

FrameBuffer::~FrameBuffer() {}

FrameBuffer::FrameBuffer(FrameBuffer&& other)
  : frame_buffer_(std::move(other.frame_buffer_)),
    depth_(std::move(other.depth_)),
    color_(std::move(other.color_)) {}

FrameBuffer& FrameBuffer::operator=(FrameBuffer&& other) {
  std::swap(frame_buffer_, other.frame_buffer_);
  std::swap(depth_, other.depth_);
  std::swap(color_, other.color_);
  return *this;
}

FrameBuffer FrameBuffer::Make() {
  return FrameBuffer(MakeUniqueFrameBuffer());
}

void FrameBuffer::Bind() {
  glBindFramebuffer(GL_FRAMEBUFFER, frame_buffer_.id());
}

void FrameBuffer::SetDepth(Texture depth) {
  FTL_DCHECK(IsBound());
  depth_ = std::move(depth);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         depth_.id(), 0);
  FTL_DCHECK(CheckStatusIfDebug());
}

void FrameBuffer::SetColor(Texture color) {
  FTL_DCHECK(IsBound());
  color_ = std::move(color);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         color_.id(), 0);
  FTL_DCHECK(CheckStatusIfDebug());
}

Texture FrameBuffer::TakeColor() {
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         0, 0);
  return std::move(color_);
}

Texture FrameBuffer::SwapColor(Texture color) {
  Texture result = std::move(color_);
  SetColor(std::move(color));
  return result;
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

bool FrameBuffer::IsBound() {
  GLint id = -1;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &id);
  return id == frame_buffer_.id();
}

}  // namespace escher
