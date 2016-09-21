// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/render/render_image.h"

#include "apps/compositor/glue/skia/ganesh_image_factory.h"
#include "lib/ftl/logging.h"
#include "third_party/skia/include/core/SkImage.h"

namespace compositor {

// Invokes the release callback when both the Generator and the RenderImage
// have been freed.  Note that the Generator may outlive the RenderImage.
class RenderImage::Releaser
    : public ftl::RefCountedThreadSafe<RenderImage::Releaser> {
 public:
  Releaser(const ftl::RefPtr<ftl::TaskRunner>& task_runner,
           const ftl::Closure& release_task)
      : task_runner_(task_runner), release_task_(release_task) {
    FTL_DCHECK(task_runner_);
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(RenderImage::Releaser);

  ~Releaser() { task_runner_->PostTask(release_task_); }

  ftl::RefPtr<ftl::TaskRunner> const task_runner_;
  ftl::Closure const release_task_;
};

class RenderImage::Generator : public mojo::skia::MailboxTextureImageGenerator {
 public:
  Generator(const ftl::RefPtr<Releaser>& releaser,
            const GLbyte mailbox_name[GL_MAILBOX_SIZE_CHROMIUM],
            GLuint sync_point,
            uint32_t width,
            uint32_t height,
            GrSurfaceOrigin origin)
      : MailboxTextureImageGenerator(mailbox_name,
                                     sync_point,
                                     width,
                                     height,
                                     origin),
        releaser_(releaser) {
    FTL_DCHECK(releaser_);
  }

  ~Generator() override {}

 private:
  ftl::RefPtr<Releaser> releaser_;
};

RenderImage::RenderImage(const sk_sp<SkImage>& image,
                         const ftl::RefPtr<Releaser>& releaser)
    : image_(image), releaser_(releaser) {
  FTL_DCHECK(image_);
  FTL_DCHECK(releaser_);
}

RenderImage::~RenderImage() {}

ftl::RefPtr<RenderImage> RenderImage::CreateFromMailboxTexture(
    const GLbyte mailbox_name[GL_MAILBOX_SIZE_CHROMIUM],
    GLuint sync_point,
    uint32_t width,
    uint32_t height,
    mojo::gfx::composition::MailboxTextureResource::Origin origin,
    const ftl::RefPtr<ftl::TaskRunner>& task_runner,
    const ftl::Closure& release_task) {
  ftl::RefPtr<Releaser> releaser =
      ftl::MakeRefCounted<Releaser>(task_runner, release_task);
  sk_sp<SkImage> image = SkImage::MakeFromGenerator(
      new Generator(releaser, mailbox_name, sync_point, width, height,
                    origin == mojo::gfx::composition::MailboxTextureResource::
                                  Origin::BOTTOM_LEFT
                        ? kBottomLeft_GrSurfaceOrigin
                        : kTopLeft_GrSurfaceOrigin));
  if (!image)
    return nullptr;

  return ftl::MakeRefCounted<RenderImage>(image, releaser);
}

}  // namespace compositor
