// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_GFX_COMPOSITOR_RENDER_RENDER_IMAGE_H_
#define SERVICES_GFX_COMPOSITOR_RENDER_RENDER_IMAGE_H_

#include <GLES2/gl2.h>
#include <GLES2/gl2extmojo.h>

#include "apps/compositor/services/interfaces/resources.mojom.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/tasks/task_runner.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkRefCnt.h"

namespace compositor {

// Describes an image which can be rendered by the compositor.
//
// Render objects are thread-safe, immutable, and reference counted.
// They have no direct references to the scene graph.
//
// TODO(jeffbrown): Generalize this beyond mailbox textures.
class RenderImage : public ftl::RefCountedThreadSafe<RenderImage> {
  class Releaser;
  class Generator;

 public:
  RenderImage(const sk_sp<SkImage>& image,
              const ftl::RefPtr<Releaser>& releaser);

  // Creates a new image backed by a mailbox texture.
  // If |sync_point| is non-zero, inserts a sync point into the command stream
  // before the image is first drawn.
  // When the last reference is released, the associated release task is
  // posted to the task runner.  Returns nullptr if the mailbox texture
  // is invalid.
  static ftl::RefPtr<RenderImage> CreateFromMailboxTexture(
      const GLbyte mailbox_name[GL_MAILBOX_SIZE_CHROMIUM],
      GLuint sync_point,
      uint32_t width,
      uint32_t height,
      mojo::gfx::composition::MailboxTextureResource::Origin origin,
      const ftl::RefPtr<ftl::TaskRunner>& task_runner,
      const ftl::Closure& release_task);

  uint32_t width() const { return image_->width(); }
  uint32_t height() const { return image_->height(); }

  // Gets the underlying image to rasterize, never null.
  const sk_sp<SkImage>& image() const { return image_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(RenderImage);

  ~RenderImage();

  sk_sp<SkImage> image_;
  ftl::RefPtr<Releaser> releaser_;

  FTL_DISALLOW_COPY_AND_ASSIGN(RenderImage);
};

}  // namespace compositor

#endif  // SERVICES_GFX_COMPOSITOR_RENDER_RENDER_IMAGE_H_
