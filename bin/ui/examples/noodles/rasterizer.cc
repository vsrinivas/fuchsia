// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/noodles/rasterizer.h"

#include "apps/mozart/examples/noodles/frame.h"
#include "apps/mozart/lib/skia/skia_surface_holder.h"
#include "lib/ftl/logging.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace examples {

constexpr uint32_t kContentImageResourceId = 1;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;

Rasterizer::Rasterizer(mojo::ApplicationConnectorPtr connector,
                       mozart::ScenePtr scene)
    : scene_(scene.Pass()) {}

Rasterizer::~Rasterizer() {}

void Rasterizer::PublishFrame(std::unique_ptr<Frame> frame) {
  FTL_DCHECK(frame);

  auto update = mozart::SceneUpdate::New();

  if (frame->size().width > 0 && frame->size().height > 0) {
    mojo::RectF bounds;
    bounds.width = frame->size().width;
    bounds.height = frame->size().height;

    mozart::SkiaSurfaceHolder surface_holder(frame->size());
    frame->Paint(surface_holder.surface()->getCanvas());
    auto content_resource = mozart::Resource::New();
    content_resource->set_image(mozart::ImageResource::New());
    content_resource->get_image()->image = surface_holder.TakeImage();
    update->resources.insert(kContentImageResourceId, content_resource.Pass());

    auto root_node = mozart::Node::New();
    root_node->op = mozart::NodeOp::New();
    root_node->op->set_image(mozart::ImageNodeOp::New());
    root_node->op->get_image()->content_rect = bounds.Clone();
    root_node->op->get_image()->image_resource_id = kContentImageResourceId;
    update->nodes.insert(kRootNodeId, root_node.Pass());
  } else {
    auto root_node = mozart::Node::New();
    update->nodes.insert(kRootNodeId, root_node.Pass());
  }

  scene_->Update(update.Pass());
  scene_->Publish(frame->TakeSceneMetadata());
}

}  // namespace examples
