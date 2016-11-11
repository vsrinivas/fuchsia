// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shapes/shapes_view.h"

#include "lib/ftl/logging.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace examples {

namespace {
constexpr uint32_t kContentImageResourceId = 1;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
}  // namespace

ShapesView::ShapesView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "Shapes") {}

ShapesView::~ShapesView() {}

void ShapesView::OnDraw() {
  FTL_DCHECK(properties());

  auto update = mozart::SceneUpdate::New();

  const mozart::Size& size = *properties()->view_layout->size;
  if (size.width > 0 && size.height > 0) {
    mozart::RectF bounds;
    bounds.width = size.width;
    bounds.height = size.height;

    // Draw the content of the view to a texture and include it as an
    // image resource in the scene.
    mozart::ImagePtr image;
    sk_sp<SkSurface> surface =
        mozart::MakeSkSurface(size, &buffer_producer_, &image);
    FTL_CHECK(surface);
    DrawContent(size, surface->getCanvas());
    auto content_resource = mozart::Resource::New();
    content_resource->set_image(mozart::ImageResource::New());
    content_resource->get_image()->image = std::move(image);
    update->resources.insert(kContentImageResourceId,
                             std::move(content_resource));

    // Add a root node to the scene graph to draw the image resource to
    // the screen such that it fills the entire view.
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

  // Submit the scene update.
  scene()->Update(std::move(update));

  // Publish the scene update, taking care to supply the expected scene version.
  scene()->Publish(CreateSceneMetadata());
}

void ShapesView::DrawContent(const mozart::Size& size, SkCanvas* canvas) {
  canvas->clear(SK_ColorCYAN);

  SkPaint paint;
  paint.setColor(SK_ColorGREEN);
  SkRect rect = SkRect::MakeWH(size.width, size.height);
  rect.inset(10, 10);
  canvas->drawRect(rect, paint);

  paint.setColor(SK_ColorRED);
  paint.setFlags(SkPaint::kAntiAlias_Flag);
  canvas->drawCircle(50, 100, 100, paint);
}

}  // namespace examples
