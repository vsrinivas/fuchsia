// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_LAUNCHER_PRESENTER_H_
#define APPS_MOZART_SRC_LAUNCHER_PRESENTER_H_

#include <memory>

#include "apps/mozart/services/composition/compositor.fidl.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/src/input_reader/input_interpreter.h"
#include "apps/mozart/src/input_reader/input_reader.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

namespace launcher {

class LauncherViewTree;

class Presenter {
 public:
  Presenter(mozart::Compositor* compositor,
            mozart::ViewManager* view_manager,
            mozart::ViewOwnerPtr view_owner);
  ~Presenter();

  void set_shutdown_callback(ftl::Closure callback) {
    shutdown_callback_ = std::move(callback);
  }

  void Show();

 private:
  mozart::Compositor* const compositor_;
  mozart::ViewManager* const view_manager_;
  mozart::ViewOwnerPtr view_owner_;
  ftl::Closure shutdown_callback_;

  mozart::RendererPtr renderer_;
  std::unique_ptr<LauncherViewTree> view_tree_;

  mozart::input::InputInterpreter input_interpreter_;
  mozart::input::InputReader input_reader_;
  mozart::PointF mouse_coordinates_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Presenter);
};

}  // namespace launcher

#endif  // APPS_MOZART_SRC_LAUNCHER_PRESENTER_H_
