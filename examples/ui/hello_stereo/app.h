// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_HELLO_STEREO_APP_H_
#define GARNET_EXAMPLES_UI_HELLO_STEREO_APP_H_

#include <lib/async-loop/cpp/loop.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

namespace hello_stereo {

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

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  async::Loop* const loop_;
  fuchsia::ui::scenic::ScenicPtr scenic_;

  std::unique_ptr<scenic::Session> session_;
  std::unique_ptr<scenic::DisplayCompositor> compositor_;

  // Time of the first update.  Animation of the "pane" content is based on the
  // time elapsed since this time.
  uint64_t start_time_ = 0;
};

}  // namespace hello_stereo

#endif  // GARNET_EXAMPLES_UI_HELLO_STEREO_APP_H_
