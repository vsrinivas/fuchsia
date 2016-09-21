// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/graph/resources.h"

#include "lib/ftl/logging.h"

namespace compositor {

Resource::Resource() {}

Resource::~Resource() {}

SceneResource::SceneResource(
    const mojo::gfx::composition::SceneToken& scene_token)
    : scene_token_(scene_token) {}

SceneResource::~SceneResource() {}

Resource::Type SceneResource::type() const {
  return Type::kScene;
}

ImageResource::ImageResource(const ftl::RefPtr<RenderImage>& image)
    : image_(image) {
  FTL_DCHECK(image);
}

ImageResource::~ImageResource() {}

Resource::Type ImageResource::type() const {
  return Type::kImage;
}

}  // namespace compositor
