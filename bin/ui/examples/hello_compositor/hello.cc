// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <mojo/system/main.h>

#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/services/composition/cpp/frame_tracker.h"
#include "apps/mozart/services/composition/interfaces/compositor.mojom.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/services/framebuffer/interfaces/framebuffer.mojom.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace {

constexpr uint32_t kSceneVersion = 1u;
constexpr uint32_t kContentImageResourceId = 1u;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;

class HelloApp : public mojo::ApplicationImplBase {
 public:
  HelloApp() {}
  ~HelloApp() override {}

  void OnInitialize() override {
    mojo::ConnectToService(shell(), "mojo:framebuffer",
                           mojo::GetProxy(&framebuffer_provider_));
    framebuffer_provider_->Create([this](
        mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
        mojo::FramebufferInfoPtr framebuffer_info) {
      FTL_CHECK(framebuffer);
      FTL_CHECK(framebuffer_info);

      framebuffer_size_ = *framebuffer_info->size;
      compositor_->CreateRenderer(
          std::move(framebuffer), std::move(framebuffer_info),
          GetProxy(&renderer_), "Hello Compositor Renderer");

      compositor_->CreateScene(GetProxy(&scene_), "Hello Compositor Scene",
                               [this](mozart::SceneTokenPtr scene_token) {
                                 auto viewport = mojo::Rect::New();
                                 viewport->width = framebuffer_size_.width;
                                 viewport->height = framebuffer_size_.height;
                                 renderer_->SetRootScene(std::move(scene_token),
                                                         kSceneVersion,
                                                         std::move(viewport));
                               });

      scene_->GetScheduler(mojo::GetProxy(&frame_scheduler_));
      ScheduleDraw();
    });

    mojo::ConnectToService(shell(), "mojo:compositor_service",
                           GetProxy(&compositor_));
  }

  void ScheduleDraw() {
    frame_scheduler_->ScheduleFrame([this](mozart::FrameInfoPtr frame_info) {
      frame_tracker_.Update(*frame_info, MojoGetTimeTicksNow());
      Draw();
    });
  }

  void Draw() {
    FTL_DCHECK(scene_);

    // Update position of circle using frame time to drive the animation.
    x_ += 40.0f * frame_tracker_.frame_time_delta().ToSecondsF();
    if (x_ >= framebuffer_size_.width)
      x_ = 0.0;

    // Draw the contents of the scene to a surface.
    mozart::ImagePtr image;
    sk_sp<SkSurface> surface = mozart::MakeSkSurface(framebuffer_size_, &image);
    FTL_CHECK(surface);

    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorBLUE);
    SkPaint paint;
    paint.setColor(0xFFFF00FF);
    paint.setAntiAlias(true);
    canvas->drawCircle(x_, y_, 200.0f, paint);
    canvas->flush();

    // Update the scene contents.
    auto update = mozart::SceneUpdate::New();
    mojo::RectF bounds;
    bounds.width = framebuffer_size_.width;
    bounds.height = framebuffer_size_.height;

    auto content_resource = mozart::Resource::New();
    content_resource->set_image(mozart::ImageResource::New());
    content_resource->get_image()->image = std::move(image);
    update->resources.insert(kContentImageResourceId, content_resource.Pass());

    auto root_node = mozart::Node::New();
    root_node->op = mozart::NodeOp::New();
    root_node->op->set_image(mozart::ImageNodeOp::New());
    root_node->op->get_image()->content_rect = bounds.Clone();
    root_node->op->get_image()->image_resource_id = kContentImageResourceId;
    update->nodes.insert(kRootNodeId, root_node.Pass());

    scene_->Update(update.Pass());

    // Publish the updated scene contents.
    auto metadata = mozart::SceneMetadata::New();
    metadata->version = kSceneVersion;
    metadata->presentation_time = frame_tracker_.frame_info().presentation_time;
    scene_->Publish(std::move(metadata));

    // Schedule the next frame of the animation.
    ScheduleDraw();
  }

 private:
  mojo::FramebufferProviderPtr framebuffer_provider_;
  mojo::Size framebuffer_size_;

  mozart::CompositorPtr compositor_;
  mozart::RendererPtr renderer_;
  mozart::ScenePtr scene_;
  mozart::FrameSchedulerPtr frame_scheduler_;
  mozart::FrameTracker frame_tracker_;

  float x_ = 400.0f;
  float y_ = 300.0f;

  FTL_DISALLOW_COPY_AND_ASSIGN(HelloApp);
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  HelloApp app;
  return mojo::RunApplication(request, &app);
}
