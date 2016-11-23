// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/noodles/rasterizer.h"

#include "apps/mozart/examples/noodles/frame.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "lib/ftl/logging.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace examples {

constexpr uint32_t kContentImageResourceId = 1;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;

Rasterizer::Rasterizer(mozart::ScenePtr scene) : scene_(std::move(scene)) {}

Rasterizer::~Rasterizer() {}

void Rasterizer::PublishFrame(std::unique_ptr<Frame> frame) {
  FTL_DCHECK(frame);

  auto update = mozart::SceneUpdate::New();

  sk_sp<SkSurface> surface;
  if (frame->size().width > 0 && frame->size().height > 0) {
    mozart::RectF bounds;
    bounds.width = frame->size().width;
    bounds.height = frame->size().height;

    // Get a surface to draw the contents.
    mozart::ImagePtr image;
    surface = mozart::MakeSkSurface(frame->size(), &buffer_producer_, &image);
    FTL_CHECK(surface);

    // Update the scene contents.
    auto content_resource = mozart::Resource::New();
    content_resource->set_image(mozart::ImageResource::New());
    content_resource->get_image()->image = std::move(image);
    update->resources.insert(kContentImageResourceId,
                             std::move(content_resource));

    auto root_node = mozart::Node::New();
    root_node->op = mozart::NodeOp::New();
    root_node->op->set_image(mozart::ImageNodeOp::New());
    root_node->op->get_image()->content_rect = bounds.Clone();
    root_node->op->get_image()->image_resource_id = kContentImageResourceId;
    update->nodes.insert(kRootNodeId, std::move(root_node));
  } else {
    auto root_node = mozart::Node::New();
    update->nodes.insert(kRootNodeId, std::move(root_node));
  }

  // Publish the updated scene contents.
  scene_->Update(std::move(update));
  scene_->Publish(frame->TakeSceneMetadata());

  // Draw the contents of the scene to a surface.
  // We do this after publishing to take advantage of pipelining.
  // The image buffer's fence is signalled automatically when the surface
  // goes out of scope.
  if (surface) {
    frame->Paint(surface->getCanvas());
  }

  buffer_producer_.Tick();
}

}  // namespace examples
