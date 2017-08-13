// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "application/lib/app/application_context.h"
#include "apps/mozart/services/fun/sketchy/canvas.fidl-common.h"
#include "apps/mozart/src/sketchy/canvas.h"
#include "escher/escher.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace sketchy_service {

class App {
 public:
  App(escher::Escher* escher);

 private:
  mtl::MessageLoop* loop_;
  std::unique_ptr<app::ApplicationContext> context_;
  mozart2::SceneManagerPtr scene_manager_;
  std::unique_ptr<mozart::client::Session> session_;
  fidl::BindingSet<sketchy::Canvas> bindings_;
  std::unique_ptr<CanvasImpl> canvas_;
};

}  // namespace sketchy_service
