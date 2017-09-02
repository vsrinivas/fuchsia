// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"

#include "lib/mtl/tasks/message_loop.h"

#include "apps/mozart/lib/scenic/client/resources.h"
#include "apps/mozart/lib/scenic/client/session.h"

namespace hello_scene_manager {

class App {
 public:
  App();

 private:
  // Called asynchronously by constructor.
  void Init(scenic::DisplayInfoPtr display_info);

  // Updates and presents the scene.  Called first by Init().  Each invocation
  // schedules another call to Update() when the result of the previous
  // presentation is asynchronously received.
  void Update(uint64_t next_presentation_time);

  void CreateExampleScene(float display_width, float display_height);

  void ReleaseSessionResources();

  void InitCheckerboardMaterial(scenic_lib::Material* uninitialized_material);

  std::unique_ptr<app::ApplicationContext> application_context_;
  mtl::MessageLoop* loop_;
  scenic::SceneManagerPtr scene_manager_;

  std::unique_ptr<scenic_lib::Session> session_;
  std::unique_ptr<scenic_lib::DisplayCompositor> compositor_;
  std::unique_ptr<scenic_lib::Camera> camera_;

  std::unique_ptr<scenic_lib::ShapeNode> rrect_node_;
  std::unique_ptr<scenic_lib::ShapeNode> clipper_1_;
  std::unique_ptr<scenic_lib::ShapeNode> clipper_2_;

  // Time of the first update.  Animation of the "pane" content is based on the
  // time elapsed since this time.
  uint64_t start_time_ = 0;
  // The camera alternates between moving toward and away from the stage.  This
  // time is the timestamp that the last change of direction occurred.
  uint64_t camera_anim_start_time_;
  bool camera_anim_returning_ = false;
};

}  // namespace hello_scene_manager
