// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_RENDER_RENDER_FRAME_H_
#define APPS_MOZART_SRC_COMPOSITOR_RENDER_RENDER_FRAME_H_

#include <unordered_set>

#include "apps/mozart/src/compositor/frame_info.h"
#include "apps/mozart/src/compositor/render/render_image.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/time/time_point.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkRefCnt.h"

class SkCanvas;
class SkPicture;

namespace compositor {

// Describes a frame to be rendered.
//
// Render objects are thread-safe, immutable, and reference counted.
// They have no direct references to the scene graph.
class RenderFrame : public ftl::RefCountedThreadSafe<RenderFrame> {
 public:
  using ImageSet = std::unordered_set<ftl::RefPtr<RenderImage>>;

  // Contains metadata about a particular |RenderFrame| used for tracing
  // and statistics.
  class Metadata {
   public:
    Metadata(const FrameInfo& frame_info, ftl::TimePoint composition_time);
    ~Metadata();

    const FrameInfo& frame_info() const { return frame_info_; }
    ftl::TimePoint composition_time() const { return composition_time_; }

   private:
    FrameInfo frame_info_;
    ftl::TimePoint composition_time_;
  };

  // Creates an empty render frame with no content.
  RenderFrame(const Metadata& metadata, const SkIRect& viewport);

  // Creates render frame backed by a picture.
  RenderFrame(const Metadata& metadata,
              const SkIRect& viewport,
              const sk_sp<SkPicture>& picture,
              ImageSet images);

  // Gets metadata about the frame.
  const Metadata& metadata() const { return metadata_; }

  // Gets the frame's viewport in pixels.
  const SkIRect& viewport() const { return viewport_; }

  // Gets the underlying picture to rasterize, or null if the frame is empty.
  const sk_sp<SkPicture>& picture() const { return picture_; }

  // Gets the images presented within this frame.
  const ImageSet& images() const { return images_; }

  // Draws the contents of the frame to a canvas.
  void Draw(SkCanvas* canvas) const;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(RenderFrame);
  friend class RenderFrameBuilder;

  ~RenderFrame();

  Metadata metadata_;
  SkIRect viewport_;
  sk_sp<SkPicture> picture_;
  ImageSet images_;

  FTL_DISALLOW_COPY_AND_ASSIGN(RenderFrame);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_RENDER_RENDER_FRAME_H_
