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
constexpr uint32_t kRootNodeId = mojo::gfx::composition::kSceneRootNodeId;
}  // namespace

ShapesView::ShapesView(
    mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
    mojo::InterfaceRequest<mojo::ui::ViewOwner> view_owner_request)
    : BaseView(app_connector.Pass(), view_owner_request.Pass(), "Shapes") {}

ShapesView::~ShapesView() {}

void ShapesView::OnDraw() {
  FTL_DCHECK(properties());

  auto update = mojo::gfx::composition::SceneUpdate::New();

  const mojo::Size& size = *properties()->view_layout->size;
  if (size.width > 0 && size.height > 0) {
    mojo::RectF bounds;
    bounds.width = size.width;
    bounds.height = size.height;

    // Draw the content of the view to a texture and include it as an
    // image resource in the scene.
    mojo::ui::SkiaSurfaceHolder surface_holder(size);
    DrawContent(size, surface_holder.surface()->getCanvas());
    auto content_resource = mojo::gfx::composition::Resource::New();
    content_resource->set_image(mojo::gfx::composition::ImageResource::New());
    content_resource->get_image()->image = surface_holder.TakeImage();
    update->resources.insert(kContentImageResourceId, content_resource.Pass());

    // Add a root node to the scene graph to draw the image resource to
    // the screen such that it fills the entire view.
    auto root_node = mojo::gfx::composition::Node::New();
    root_node->op = mojo::gfx::composition::NodeOp::New();
    root_node->op->set_image(mojo::gfx::composition::ImageNodeOp::New());
    root_node->op->get_image()->content_rect = bounds.Clone();
    root_node->op->get_image()->image_resource_id = kContentImageResourceId;
    update->nodes.insert(kRootNodeId, root_node.Pass());
  } else {
    auto root_node = mojo::gfx::composition::Node::New();
    update->nodes.insert(kRootNodeId, root_node.Pass());
  }

  // Submit the scene update.
  scene()->Update(update.Pass());

  // Publish the scene update, taking care to supply the expected scene version.
  scene()->Publish(CreateSceneMetadata());
}

void ShapesView::DrawContent(const mojo::Size& size, SkCanvas* canvas) {
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
