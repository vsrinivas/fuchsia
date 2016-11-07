// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>

#include <algorithm>
#include <string>

#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/lib/view_framework/view_provider_app.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
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
      mozart::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(view_manager),
                 std::move(view_owner_request),
                 "Spinning Square") {}

  ~SpinningSquareView() override {}

 private:
  // |BaseView|:
  void OnDraw() override {
    FTL_DCHECK(properties());

    auto update = mozart::SceneUpdate::New();

    const mozart::Size& size = *properties()->view_layout->size;
    if (size.width > 0 && size.height > 0) {
      mozart::RectF bounds;
      bounds.width = size.width;
      bounds.height = size.height;

      // Draw the contents of the scene to a surface.
      mozart::ImagePtr image;
      sk_sp<SkSurface> surface = mozart::MakeSkSurface(size, &image);
      FTL_CHECK(surface);
      DrawContent(surface->getCanvas(), size);

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
    scene()->Update(std::move(update));
    scene()->Publish(CreateSceneMetadata());

    // Schedule the next frame of the animation.
    Invalidate();
  }

  void DrawContent(SkCanvas* canvas, const mozart::Size& size) {
    canvas->clear(SK_ColorBLUE);
    canvas->translate(size.width / 2, size.height / 2);
    float t =
        fmod(frame_tracker().presentation_time().ToEpochDelta().ToSecondsF() *
                 kSpeed,
             1.f);
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

}  // namespace examples

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;

  mozart::ViewProviderApp app([](mozart::ViewContext view_context) {
    return std::make_unique<examples::SpinningSquareView>(
        std::move(view_context.view_manager),
        std::move(view_context.view_owner_request));
  });

  loop.Run();
  return 0;
}
