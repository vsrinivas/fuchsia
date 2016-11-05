// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_EXAMPLES_NOODLES_FRAME_H_
#define APPS_MOZART_EXAMPLES_NOODLES_FRAME_H_

#include "apps/mozart/services/composition/scenes.fidl.h"
#include "apps/mozart/services/geometry/geometry.fidl.h"
#include "lib/ftl/macros.h"
#include "third_party/skia/include/core/SkRefCnt.h"

class SkCanvas;
class SkPicture;

namespace examples {

// A frame of content to be rasterized.
// Instances of this object are created by the view's thread and sent to
// the rasterizer's thread to be drawn.
class Frame {
 public:
  Frame(const mozart::Size& size,
        sk_sp<SkPicture> picture,
        mozart::SceneMetadataPtr scene_metadata);
  ~Frame();

  const mozart::Size& size() { return size_; }

  mozart::SceneMetadataPtr TakeSceneMetadata() {
    return std::move(scene_metadata_);
  }

  void Paint(SkCanvas* canvas);

 private:
  mozart::Size size_;
  sk_sp<SkPicture> picture_;
  mozart::SceneMetadataPtr scene_metadata_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Frame);
};

}  // namespace examples

#endif  // APPS_MOZART_EXAMPLES_NOODLES_FRAME_H_
