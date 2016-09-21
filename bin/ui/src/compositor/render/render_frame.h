// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_GFX_COMPOSITOR_RENDER_RENDER_FRAME_H_
#define SERVICES_GFX_COMPOSITOR_RENDER_RENDER_FRAME_H_

#include "apps/compositor/services/interfaces/scheduling.mojom.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
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
  // Contains metadata about a particular |RenderFrame| used for tracing
  // and statistics.
  class Metadata {
   public:
    Metadata(const mojo::gfx::composition::FrameInfo& frame_info,
             int64_t composition_time);
    ~Metadata();

    const mojo::gfx::composition::FrameInfo& frame_info() const {
      return frame_info_;
    }
    int64_t composition_time() const { return composition_time_; }

   private:
    mojo::gfx::composition::FrameInfo frame_info_;
    int64_t composition_time_;
  };

  // Creates an empty render frame with no content.
  RenderFrame(const Metadata& metadata, const SkIRect& viewport);

  // Creates render frame backed by a picture.
  RenderFrame(const Metadata& metadata,
              const SkIRect& viewport,
              const sk_sp<SkPicture>& picture);

  // Gets metadata about the frame.
  const Metadata& metadata() const { return metadata_; }

  // Gets the frame's viewport in pixels.
  const SkIRect& viewport() const { return viewport_; }

  // Gets the underlying picture to rasterize, or null if the frame is empty.
  const sk_sp<SkPicture>& picture() const { return picture_; }

  // Draws the contents of the frame to a canvas.
  void Draw(SkCanvas* canvas) const;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(RenderFrame);
  friend class RenderFrameBuilder;

  ~RenderFrame();

  Metadata metadata_;
  SkIRect viewport_;
  sk_sp<SkPicture> picture_;

  FTL_DISALLOW_COPY_AND_ASSIGN(RenderFrame);
};

}  // namespace compositor

#endif  // SERVICES_GFX_COMPOSITOR_RENDER_RENDER_FRAME_H_
