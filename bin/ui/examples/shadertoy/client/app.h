// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "application/lib/app/application_context.h"
#include "apps/mozart/examples/shadertoy/client/example_scene.h"
#include "apps/mozart/examples/shadertoy/service/services/shadertoy.fidl.h"
#include "apps/mozart/examples/shadertoy/service/services/shadertoy_factory.fidl.h"

namespace shadertoy_client {

// Graphical demo that interacts with the Shadertoy and SceneManager services
// to animate a ShadertoyClientScene.
class App {
 public:
  App();

  // Called asynchronously by constructor.
  void Init(mozart2::DisplayInfoPtr display_info);

  // Updates and presents the scene.  Called first by Init().  Each invocation
  // schedules another call to Update() when the result of the previous
  // presentation is asynchronously received.
  void Update(uint64_t next_presentation_time);

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  mtl::MessageLoop* loop_;
  mozart2::SceneManagerPtr scene_manager_;
  mozart::client::Session session_;
  mozart::example::ShadertoyFactoryPtr shadertoy_factory_;
  mozart::example::ShadertoyPtr shadertoy_;

  std::unique_ptr<ExampleScene> scene_;

  uint64_t start_time_ = 0;
};

}  // namespace shadertoy_client
