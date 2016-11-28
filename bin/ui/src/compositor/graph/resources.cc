// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/graph/resources.h"

#include "apps/mozart/services/composition/cpp/formatting.h"
#include "lib/ftl/logging.h"

namespace compositor {

Resource::Resource() {}

Resource::~Resource() {}

SceneResource::SceneResource(const mozart::SceneToken& scene_token)
    : scene_token_(scene_token) {}

SceneResource::~SceneResource() {}

Resource::Type SceneResource::type() const {
  return Type::kScene;
}

void SceneResource::Dump(tracing::Dump* dump) const {
  dump->out() << "SceneResource {scene_token=" << scene_token_ << "}";
}

ImageResource::ImageResource(ftl::RefPtr<RenderImage> image)
    : image_(std::move(image)) {
  FTL_DCHECK(image_);
}

ImageResource::~ImageResource() {}

Resource::Type ImageResource::type() const {
  return Type::kImage;
}

void ImageResource::Dump(tracing::Dump* dump) const {
  dump->out() << "ImageResource {width=" << image_->width()
              << ", height=" << image_->height() << "}";
}

}  // namespace compositor
