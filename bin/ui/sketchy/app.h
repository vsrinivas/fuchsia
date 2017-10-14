// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/sketchy/canvas.h"
#include "lib/app/cpp/application_context.h"
#include "lib/escher/escher.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/ui/fun/sketchy/fidl/canvas.fidl-common.h"

namespace sketchy_service {

class App {
 public:
  App(escher::Escher* escher);

 private:
  fsl::MessageLoop* loop_;
  std::unique_ptr<app::ApplicationContext> context_;
  scenic::SceneManagerPtr scene_manager_;
  std::unique_ptr<scenic_lib::Session> session_;
  fidl::BindingSet<sketchy::Canvas> bindings_;
  std::unique_ptr<CanvasImpl> canvas_;
};

}  // namespace sketchy_service
