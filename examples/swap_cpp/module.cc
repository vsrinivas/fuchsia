// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/examples/swap_cpp/module.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "third_party/skia/include/core/SkCanvas.h"

namespace modular_example {

constexpr uint32_t kContentImageResourceId = 1;

ModuleView::ModuleView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    SkColor color)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "ModuleView"),
      color_(color) {}

void ModuleView::OnDraw() {
  auto update = mozart::SceneUpdate::New();
  auto root_node = mozart::Node::New();

  FTL_DCHECK(properties());
  const mozart::Size& size = *properties()->view_layout->size;
  if (size.width > 0 && size.height > 0) {
    mozart::ImagePtr image;
    sk_sp<SkSurface> surface =
        mozart::MakeSkSurface(size, &buffer_producer_, &image);

    FTL_CHECK(surface);
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorBLUE);
    canvas->translate(size.width / 2, size.height / 2);
    SkPaint paint;
    paint.setColor(color_);
    paint.setAntiAlias(true);
    float d = std::min(size.width, size.height) / 4;
    canvas->drawRect(SkRect::MakeLTRB(-d, -d, d, d), paint);
    canvas->flush();

    auto content_resource = mozart::Resource::New();
    content_resource->set_image(mozart::ImageResource::New());
    content_resource->get_image()->image = std::move(image);
    update->resources.insert(kContentImageResourceId,
                             std::move(content_resource));

    mozart::RectF bounds;
    bounds.width = size.width;
    bounds.height = size.height;
    root_node->op = mozart::NodeOp::New();
    root_node->op->set_image(mozart::ImageNodeOp::New());
    root_node->op->get_image()->content_rect = bounds.Clone();
    root_node->op->get_image()->image_resource_id = kContentImageResourceId;
  }

  update->nodes.insert(mozart::kSceneRootNodeId, std::move(root_node));
  scene()->Update(std::move(update));
  scene()->Publish(CreateSceneMetadata());
  buffer_producer_.Tick();
}

ModuleApp::ModuleApp(CreateViewCallback create) : create_(create) {}

void ModuleApp::CreateView(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    fidl::InterfaceRequest<app::ServiceProvider> services) {
  view_.reset(create_(
      application_context()->ConnectToEnvironmentService<mozart::ViewManager>(),
      std::move(view_owner_request)));
}

void ModuleApp::Initialize(
    fidl::InterfaceHandle<modular::ModuleContext> moduleContext,
    fidl::InterfaceHandle<app::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) {}

void ModuleApp::Stop(const StopCallback& done) {
  done();
}

}  // namespace modular_example
