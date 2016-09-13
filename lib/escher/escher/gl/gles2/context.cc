// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/gl/gles2/context.h"

#include <string>

namespace escher {
namespace gles2 {

Context::Context() : frame_buffer_(MakeUniqueFrameBuffer()) {}

Context::~Context() {}

void Context::BeginRenderPass(const RenderPassSpec& spec, const char* name) {
  FTL_DCHECK(!is_recording_);
  is_recording_ = true;

  glPushGroupMarkerEXT(static_cast<GLsizei>(strlen(name) + 1), name);

  glBindFramebuffer(GL_FRAMEBUFFER, frame_buffer_.id());

  GLbitfield clear_bits = 0;

  // Update color attachments.
  for (size_t i = 0; i < RenderPassSpec::kNumColorAttachments; ++i) {
    const auto& attachment = spec.color[i];
    const auto& current_attachment = current_spec_.color[i];

    // TODO(jjosh): unsupported usages.
    FTL_DCHECK(attachment.load_action !=
        RenderPassSpec::Attachment::LoadAction::kLoad);
    FTL_DCHECK(attachment.store_action !=
        RenderPassSpec::Attachment::StoreAction::kMultisampleResolve);

    if (attachment.texture) {
      if (ShouldClearAttachment(attachment)) {
        clear_bits |= GL_COLOR_BUFFER_BIT;
        // TODO(jjosh): multiple render targets... how to specify a different
        // clear color for each one?
        FTL_DCHECK(RenderPassSpec::kNumColorAttachments == 1);
        const vec4& color = attachment.clear_color;
        glClearColor(color.r, color.g, color.b, color.a);
      }

      if (attachment.texture == current_attachment.texture) continue;

      // If we're replacing a texture with renderbuffer (or vice versa) then
      // we must first remove the existing attachment, instead of simply
      // stomping it with the new one.
      if (attachment.texture.IsRenderbuffer() !=
          current_attachment.texture.IsRenderbuffer()) {
        DetachTexture(current_attachment.texture, GL_COLOR_ATTACHMENT0 + i);
      }

      AttachTexture(attachment.texture, GL_COLOR_ATTACHMENT0 + i);
    } else {
      DetachTexture(current_attachment.texture, GL_COLOR_ATTACHMENT0 + i);
    }
  }

  // Update depth attachment.
  {
    const auto& attachment = spec.depth;
    const auto& current_attachment = current_spec_.depth;

    // TODO(jjosh): unsupported usages.
    FTL_DCHECK(attachment.load_action !=
        RenderPassSpec::Attachment::LoadAction::kLoad);
    FTL_DCHECK(attachment.store_action !=
        RenderPassSpec::Attachment::StoreAction::kMultisampleResolve);

    if (attachment.texture) {
      if (ShouldClearAttachment(attachment)) {
        clear_bits |= GL_DEPTH_BUFFER_BIT;
        glClearDepth(attachment.clear_depth);
      }

      if (attachment.texture != current_attachment.texture) {
        // If we're replacing a texture with renderbuffer (or vice versa) then
        // we must first remove the existing attachment, instead of simply
        // stomping it with the new one.
        if (attachment.texture.IsRenderbuffer() !=
            current_attachment.texture.IsRenderbuffer()) {
          DetachTexture(current_attachment.texture, GL_DEPTH_ATTACHMENT);
        }

        AttachTexture(attachment.texture, GL_DEPTH_ATTACHMENT);
      }
    } else {
      DetachTexture(current_attachment.texture, GL_DEPTH_ATTACHMENT);
    }
  }

  glClear(clear_bits);
}

void Context::EndRenderPass() {
  FTL_DCHECK(is_recording_);
  is_recording_ = false;

  glPopGroupMarkerEXT();
}

void Context::AttachTexture(
    const Texture& texture, GLenum attachment_point) const {
  FTL_DCHECK(texture);
  if (texture.IsRenderbuffer()) {
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, attachment_point, GL_RENDERBUFFER, texture.id());
  } else {
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, attachment_point, texture.GetGLTarget(), texture.id(), 0);
  }
}

void Context::DetachTexture(
    const Texture& texture, GLenum attachment_point) const {
  if (!texture) {
    return;
  } else if (texture.IsRenderbuffer()) {
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, attachment_point, GL_RENDERBUFFER, 0);
  } else {
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, attachment_point, texture.GetGLTarget(), 0, 0);
  }
}

bool Context::ShouldClearAttachment(
    const RenderPassSpec::Attachment& attachment) const {
  switch (attachment.load_action) {
    case RenderPassSpec::Attachment::LoadAction::kWhatever:
      // On GPUs with tiled architectures, it is faster to clear the target
      // attachment than to load pixels from memory.
      return true;
    case RenderPassSpec::Attachment::LoadAction::kLoad:
      return false;
    case RenderPassSpec::Attachment::LoadAction::kClear:
      return true;
  }
}

}  // namespace gles2
}  // namespace escher
