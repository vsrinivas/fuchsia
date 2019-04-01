// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_LAB_POSE_BUFFER_PROVIDER_APP_H_
#define GARNET_EXAMPLES_UI_LAB_POSE_BUFFER_PROVIDER_APP_H_

#include <lib/async-loop/cpp/loop.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

namespace pose_buffer_presenter {

class App {
 public:
  App(async::Loop* loop);

 private:
  // Called asynchronously by constructor.
  void Init(fuchsia::ui::gfx::DisplayInfo display_info);

  // Updates and presents the scene.  Called first by Init().  Each invocation
  // schedules another call to Update() when the result of the previous
  // presentation is asynchronously received.
  void Update(uint64_t next_presentation_time);

  // Parameters expressed in pixels
  void CreateExampleScene(float display_width, float display_height);
  void ConfigurePoseBuffer();

  void ReleaseSessionResources();

  std::unique_ptr<component::StartupContext> startup_context_;
  async::Loop* loop_;
  fuchsia::ui::scenic::ScenicPtr scenic_;

  std::unique_ptr<scenic::Session> session_;
  std::unique_ptr<scenic::DisplayCompositor> compositor_;
  std::unique_ptr<scenic::StereoCamera> camera_;
  std::unique_ptr<scenic::ShapeNode> cube_node_;

  zx::vmo pose_buffer_vmo_;

  // Time of the first update.  Animation of the "pane" content is based on the
  // time elapsed since this time.
  zx_time_t start_time_ = 0;

  component::Services services_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::ui::gfx::PoseBufferProviderPtr provider_;
};

}  // namespace pose_buffer_presenter

#endif  // GARNET_EXAMPLES_UI_LAB_POSE_BUFFER_PROVIDER_APP_H_
