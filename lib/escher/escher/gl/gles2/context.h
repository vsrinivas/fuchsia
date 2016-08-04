// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/gl/render_pass_spec.h"
#include "escher/gl/unique_frame_buffer.h"

namespace escher {
namespace gles2 {

// This isn't (only) an OpenGL context.  It implements an abstraction that works
// for other graphics APIs, such as Vulkan and Metal.
class Context {
 public:
  Context();
  ~Context();

  void BeginRenderPass(const RenderPassSpec& spec, const char* name);
  void EndRenderPass();

 private:
  bool ShouldClearAttachment(
      const RenderPassSpec::Attachment& attachment) const;

  void AttachTexture(const Texture& texture, GLenum attachment_point) const;
  void DetachTexture(const Texture& texture, GLenum attachment_point) const;

  UniqueFrameBuffer frame_buffer_;
  bool is_recording_ = false;

  RenderPassSpec current_spec_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Context);
};

}  // namespace gles2
}  // namespace escher
