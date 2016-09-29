// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/render/render_image.h"

#include "lib/ftl/logging.h"
#include "mojo/public/cpp/system/buffer.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkPixmap.h"

namespace compositor {
namespace {
// TODO(jeffbrown): Determine and document a more appropriate size limit
// for transferred images as part of the image pipe abstraction instead.
const int32_t kMaxImageWidth = 65536;
const int32_t kMaxImageHeight = 65536;
}  // namespace

// Invokes the release callback when both the SkImage and the RenderImage
// have been freed.  Note that the SkImage may outlive the RenderImage.
class RenderImage::Releaser
    : public ftl::RefCountedThreadSafe<RenderImage::Releaser> {
 public:
  Releaser(const ftl::RefPtr<ftl::TaskRunner>& task_runner,
           const ftl::Closure& release_task,
           void* buffer)
      : task_runner_(task_runner),
        release_task_(release_task),
        buffer_(buffer) {
    FTL_DCHECK(task_runner_);
    FTL_DCHECK(release_task_);
    FTL_DCHECK(buffer_);
  }

  static void ReleaseImage(const void* pixels, void* releaser) {
    static_cast<Releaser*>(releaser)->Release();
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(RenderImage::Releaser);

  ~Releaser() {
    task_runner_->PostTask(release_task_);
    FTL_CHECK(MOJO_RESULT_OK == mojo::UnmapBuffer(buffer_));
  }

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  ftl::Closure release_task_;
  void* buffer_;
};

RenderImage::RenderImage(const sk_sp<SkImage>& image,
                         const ftl::RefPtr<Releaser>& releaser)
    : image_(image), releaser_(releaser) {
  FTL_DCHECK(image_);
  FTL_DCHECK(releaser_);
}

RenderImage::~RenderImage() {}

ftl::RefPtr<RenderImage> RenderImage::CreateFromImage(
    mozart::ImagePtr image,
    const ftl::RefPtr<ftl::TaskRunner>& task_runner,
    const ftl::Closure& release_task) {
  FTL_DCHECK(image);
  FTL_DCHECK(image->size);

  const int32_t width = image->size->width;
  const int32_t height = image->size->height;
  if (width < 1 || width > kMaxImageWidth || height < 1 ||
      height > kMaxImageHeight) {
    FTL_LOG(ERROR) << "Invalid image size: width=" << width
                   << ", height=" << height;
    return nullptr;
  }

  SkColorType sk_color_type;
  switch (image->pixel_format) {
    case mozart::Image::PixelFormat::B8G8R8A8:
      sk_color_type = kBGRA_8888_SkColorType;
      break;
    default:
      FTL_LOG(ERROR) << "Unsupported image pixel format: pixel_format="
                     << image->pixel_format;
      return nullptr;
  }

  SkAlphaType sk_alpha_type;
  switch (image->alpha_format) {
    case mozart::Image::AlphaFormat::OPAQUE:
      sk_alpha_type = kOpaque_SkAlphaType;
      break;
    case mozart::Image::AlphaFormat::PREMULTIPLIED:
      sk_alpha_type = kPremul_SkAlphaType;
      break;
    case mozart::Image::AlphaFormat::NON_PREMULTIPLIED:
      sk_alpha_type = kUnpremul_SkAlphaType;
      break;
    default:
      FTL_LOG(ERROR) << "Unsupported image alpha format: alpha_format="
                     << image->alpha_format;
      return nullptr;
  }

  sk_sp<SkColorSpace> sk_color_space;
  switch (image->color_space) {
    case mozart::Image::ColorSpace::SRGB:
      sk_color_space = SkColorSpace::NewNamed(SkColorSpace::kSRGB_Named);
      break;
    default:
      FTL_LOG(ERROR) << "Unsupported image color space: color_space="
                     << image->color_space;
      return nullptr;
  }

  void* buffer;
  MojoResult result = mojo::MapBuffer(image->buffer.get(), image->offset,
                                      image->stride * height, &buffer,
                                      MOJO_MAP_BUFFER_FLAG_NONE);
  if (result != MOJO_RESULT_OK) {
    FTL_LOG(ERROR) << "Failed to memory map image buffer: result=" << result;
    return nullptr;
  }
  FTL_DCHECK(buffer);

  // Note: releaser takes ownership of buffer
  ftl::RefPtr<Releaser> releaser =
      ftl::MakeRefCounted<Releaser>(task_runner, release_task, buffer);

  SkPixmap sk_pixmap(
      SkImageInfo::Make(image->size->width, image->size->height, sk_color_type,
                        sk_alpha_type, sk_color_space),
      buffer, image->stride);
  sk_sp<SkImage> sk_image = SkImage::MakeFromRaster(
      sk_pixmap, &Releaser::ReleaseImage, releaser.get());
  if (!sk_image) {
    FTL_LOG(ERROR) << "Failed to create SkImage";
    return nullptr;
  }
  releaser.get()->AddRef();  // held by SkImage

  return ftl::MakeRefCounted<RenderImage>(sk_image, releaser);
}

}  // namespace compositor
