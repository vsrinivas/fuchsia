// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/composition/compositor.fidl-sync.h"
#include "apps/mozart/services/composition/scheduling.fidl-sync.h"
#include "lib/fidl/cpp/application/application_test_base.h"
#include "lib/fidl/cpp/application/connect.h"
#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"
#include "lib/ftl/macros.h"
#include "mojo/services/gpu/context_provider.fidl.h"
#include "mojo/services/native_viewport/native_viewport.fidl.h"

namespace test {

using SynchronousCompositorPtr = fidl::SynchronousInterfacePtr<Compositor>;

using SynchronousFrameSchedulerPtr =
    fidl::SynchronousInterfacePtr<FrameScheduler>;

class SchedulingTest : public fidl::test::ApplicationTestBase {
 public:
  SchedulingTest() {}

 protected:
  void SetUp() override {
    fidl::test::ApplicationTestBase::SetUp();

    fidl::ConnectToService(shell(), "mojo:native_viewport_service",
                           viewport_.NewRequest());
    auto size = mozart::Size::New();
    size->width = 320;
    size->height = 640;
    auto configuration = fidl::SurfaceConfiguration::New();
    viewport_->Create(std::move(size), std::move(configuration),
                      [](fidl::ViewportMetricsPtr metrics) {

                      });
    viewport_->Show();

    fidl::ContextProviderPtr context_provider;
    viewport_->GetContextProvider(context_provider.NewRequest());

    fidl::ConnectToService(shell(), "mojo:compositor_service",
                           fidl::GetSynchronousProxy(&compositor_));
    compositor_->CreateRenderer(std::move(context_provider),
                                renderer_.NewRequest(), "SchedulingTest");
  }

  void TestScheduler(SynchronousFrameSchedulerPtr scheduler) {
    FrameInfoPtr frame_info1;
    ASSERT_TRUE(scheduler->ScheduleFrame(&frame_info1));
    AssertValidFrameInfo(frame_info1.get());

    FrameInfoPtr frame_info2;
    ASSERT_TRUE(scheduler->ScheduleFrame(&frame_info2));
    AssertValidFrameInfo(frame_info2.get());

    EXPECT_GT(frame_info2->base_time, frame_info1->base_time);
    EXPECT_GT(frame_info2->presentation_time, frame_info1->presentation_time);
  }

  void AssertValidFrameInfo(FrameInfo* frame_info) {
    ASSERT_NE(nullptr, frame_info);
    EXPECT_LT(frame_info->base_time, MojoGetTimeTicksNow());
    EXPECT_GT(frame_info->presentation_interval, 0u);
    EXPECT_GT(frame_info->publish_deadline, frame_info->base_time);
    EXPECT_GT(frame_info->presentation_time, frame_info->publish_deadline);
  }

  fidl::NativeViewportPtr viewport_;
  SynchronousCompositorPtr compositor_;
  RendererPtr renderer_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(SchedulingTest);
};

namespace {

TEST_F(SchedulingTest, RendererScheduler) {
  SynchronousFrameSchedulerPtr scheduler;
  renderer_->GetScheduler(fidl::GetSynchronousProxy(&scheduler));
  TestScheduler(std::move(scheduler));
}

// Test what happens when a scene is not attached to a renderer.
// It should still receive scheduled frame updates occasionally albeit
// at some indeterminate rate (enough to keep the scene from hanging).
TEST_F(SchedulingTest, OrphanedSceneScheduler) {
  ScenePtr scene;
  SceneTokenPtr scene_token;
  compositor_->CreateScene(scene.NewRequest(), "SchedulingTest",
                           &scene_token);

  SynchronousFrameSchedulerPtr scheduler;
  scene->GetScheduler(fidl::GetSynchronousProxy(&scheduler));
  TestScheduler(std::move(scheduler));
}

// Test what happens when a scene is attached to a renderer.
// It should receive scheduled frame updates at a rate determined
// by the renderer.
TEST_F(SchedulingTest, RootSceneScheduler) {
  ScenePtr scene;
  SceneTokenPtr scene_token;
  compositor_->CreateScene(scene.NewRequest(), "SchedulingTest",
                           &scene_token);

  auto viewport = mozart::Rect::New();
  viewport->width = 1;
  viewport->height = 1;
  renderer_->SetRootScene(std::move(scene_token), kSceneVersionNone,
                          std::move(viewport));

  SynchronousFrameSchedulerPtr scheduler;
  scene->GetScheduler(fidl::GetSynchronousProxy(&scheduler));
  TestScheduler(std::move(scheduler));
}

}  // namespace
}  // namespace mozart
