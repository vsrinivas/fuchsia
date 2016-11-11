// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/services/buffers/cpp/buffer_producer.h"
#include "apps/mozart/services/composition/compositor.fidl.h"
#include "apps/mozart/services/composition/cpp/frame_tracker.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_point.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace examples {

constexpr uint32_t kSceneVersion = 1u;
constexpr uint32_t kContentImageResourceId = 1u;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;

class HelloApp {
 public:
  HelloApp()
      : application_context_(
            modular::ApplicationContext::CreateFromStartupInfo()) {
    compositor_ =
        application_context_->ConnectToEnvironmentService<mozart::Compositor>();
    compositor_->CreateRenderer(GetProxy(&renderer_),
                                "Hello Compositor Renderer");

    renderer_->GetDisplayInfo([this](mozart::DisplayInfoPtr display_info) {
      FTL_DCHECK(display_info);
      display_info_ = std::move(display_info);

      compositor_->CreateScene(GetProxy(&scene_), "Hello Compositor Scene",
                               [this](mozart::SceneTokenPtr scene_token) {
                                 auto viewport = mozart::Rect::New();
                                 viewport->width = display_info_->size->width;
                                 viewport->height = display_info_->size->height;
                                 renderer_->SetRootScene(std::move(scene_token),
                                                         kSceneVersion,
                                                         std::move(viewport));
                               });

      scene_->GetScheduler(fidl::GetProxy(&frame_scheduler_));
      ScheduleDraw();
    });
  }

  void ScheduleDraw() {
    frame_scheduler_->ScheduleFrame([this](mozart::FrameInfoPtr frame_info) {
      frame_tracker_.Update(*frame_info, ftl::TimePoint::Now());
      Draw();
    });
  }

  void Draw() {
    FTL_DCHECK(display_info_);
    FTL_DCHECK(scene_);

    // Animate the position of the circle.
    const mozart::Size& size = *display_info_->size;
    x_ += 40.0f * frame_tracker_.presentation_time_delta().ToSecondsF();
    y_ += 40.0f * frame_tracker_.presentation_time_delta().ToSecondsF();
    if (x_ >= size.width)
      x_ = 0.0;
    if (y_ >= size.height)
      y_ = 0.0;

    // Draw the contents of the scene to a surface.
    mozart::ImagePtr image;
    sk_sp<SkSurface> surface =
        mozart::MakeSkSurface(size, &buffer_producer_, &image);
    FTL_CHECK(surface);

    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorBLUE);
    SkPaint paint;
    paint.setColor(0xFFFF00FF);
    paint.setAntiAlias(true);
    canvas->drawCircle(x_, y_, std::min(size.width, size.height) / 10, paint);
    canvas->flush();

    // Update the scene contents.
    auto update = mozart::SceneUpdate::New();
    mozart::RectF bounds;
    bounds.width = size.width;
    bounds.height = size.height;

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

    scene_->Update(std::move(update));

    // Publish the updated scene contents.
    auto metadata = mozart::SceneMetadata::New();
    metadata->version = kSceneVersion;
    metadata->presentation_time = frame_tracker_.frame_info().presentation_time;
    scene_->Publish(std::move(metadata));

    // Schedule the next frame of the animation.
    ScheduleDraw();
  }

 private:
  std::unique_ptr<modular::ApplicationContext> application_context_;

  mozart::CompositorPtr compositor_;
  mozart::RendererPtr renderer_;
  mozart::DisplayInfoPtr display_info_;
  mozart::ScenePtr scene_;
  mozart::FrameSchedulerPtr frame_scheduler_;
  mozart::FrameTracker frame_tracker_;
  mozart::BufferProducer buffer_producer_;

  float x_ = 0.f;
  float y_ = 0.f;

  FTL_DISALLOW_COPY_AND_ASSIGN(HelloApp);
};

}  // namespace examples

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;

  examples::HelloApp app;

  loop.Run();
  return 0;
}
