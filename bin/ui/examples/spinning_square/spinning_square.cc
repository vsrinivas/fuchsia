// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>
#include <mojo/system/main.h>

#include <algorithm>
#include <string>

#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/lib/view_framework/skia/skia_surface_holder.h"
#include "apps/mozart/lib/view_framework/view_provider_app.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRect.h"

namespace examples {

namespace {
constexpr uint32_t kContentImageResourceId = 1;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;

constexpr float kSpeed = 0.25f;
}  // namespace

class SpinningSquareView : public mozart::BaseView {
 public:
  SpinningSquareView(
      mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(app_connector.Pass(),
                 view_owner_request.Pass(),
                 "Spinning Square") {}

  ~SpinningSquareView() override {}

 private:
  // |BaseView|:
  void OnDraw() override {
    FTL_DCHECK(properties());

    auto update = mozart::SceneUpdate::New();

    const mojo::Size& size = *properties()->view_layout->size;
    if (size.width > 0 && size.height > 0) {
      mojo::RectF bounds;
      bounds.width = size.width;
      bounds.height = size.height;

      mozart::SkiaSurfaceHolder surface_holder(size);
      DrawContent(surface_holder.surface()->getCanvas(), size);
      auto content_resource = mozart::Resource::New();
      content_resource->set_image(mozart::ImageResource::New());
      content_resource->get_image()->image = surface_holder.TakeImage();
      update->resources.insert(kContentImageResourceId,
                               content_resource.Pass());

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

    scene()->Update(update.Pass());
    scene()->Publish(CreateSceneMetadata());

    Invalidate();
  }

  void DrawContent(SkCanvas* canvas, const mojo::Size& size) {
    canvas->clear(SK_ColorBLUE);
    canvas->translate(size.width / 2, size.height / 2);
    float t =
        fmod(frame_tracker().frame_info().frame_time * 0.000001f * kSpeed, 1.f);
    canvas->rotate(360.f * t);
    SkPaint paint;
    paint.setColor(0xFFFF00FF);
    paint.setAntiAlias(true);
    float d = std::min(size.width, size.height) / 4;
    canvas->drawRect(SkRect::MakeLTRB(-d, -d, d, d), paint);
    canvas->flush();
  }

  FTL_DISALLOW_COPY_AND_ASSIGN(SpinningSquareView);
};

class SpinningSquareApp : public mozart::ViewProviderApp {
 public:
  SpinningSquareApp() {}
  ~SpinningSquareApp() override {}

  void CreateView(
      const std::string& connection_url,
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      mojo::InterfaceRequest<mojo::ServiceProvider> services) override {
    new SpinningSquareView(mojo::CreateApplicationConnector(shell()),
                           view_owner_request.Pass());
  }

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(SpinningSquareApp);
};

}  // namespace examples

MojoResult MojoMain(MojoHandle application_request) {
  examples::SpinningSquareApp app;
  return mojo::RunApplication(application_request, &app);
}
