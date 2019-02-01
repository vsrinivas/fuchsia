// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_HELLO_SCENIC_APP_H_
#define GARNET_EXAMPLES_UI_HELLO_SCENIC_APP_H_

#include <lib/async-loop/cpp/loop.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

namespace hello_scenic {

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

  void CreateExampleScene(float display_width, float display_height);

  void ReleaseSessionResources();

  void InitCheckerboardMaterial(scenic::Material* uninitialized_material);

  std::unique_ptr<component::StartupContext> startup_context_;
  async::Loop* const loop_;
  fuchsia::ui::scenic::ScenicPtr scenic_;

  std::unique_ptr<scenic::Session> session_;
  std::unique_ptr<scenic::DisplayCompositor> compositor_;
  std::unique_ptr<scenic::Camera> camera_;

  std::unique_ptr<scenic::ShapeNode> rrect_node_;
  std::unique_ptr<scenic::ShapeNode> clipper_1_;
  std::unique_ptr<scenic::ShapeNode> clipper_2_;

  // Time of the first update.  Animation of the "pane" content is based on the
  // time elapsed since this time.
  uint64_t start_time_ = 0;
  // The camera alternates between moving toward and away from the stage.  This
  // time is the timestamp that the last change of direction occurred.
  uint64_t camera_anim_start_time_;
  bool camera_anim_returning_ = false;
};

}  // namespace hello_scenic

#endif  // GARNET_EXAMPLES_UI_HELLO_SCENIC_APP_H_
