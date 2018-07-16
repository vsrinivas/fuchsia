// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_APP_H_
#define GARNET_BIN_UI_SKETCHY_APP_H_

#include <fuchsia/ui/sketchy/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/ui/sketchy/canvas.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/escher/escher.h"
#include "lib/fidl/cpp/binding_set.h"

namespace sketchy_service {

class App {
 public:
  App(async::Loop* loop, escher::EscherWeakPtr escher);

 private:
  async::Loop* const loop_;
  std::unique_ptr<component::StartupContext> context_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
  std::unique_ptr<scenic::Session> session_;
  fidl::BindingSet<::fuchsia::ui::sketchy::Canvas> bindings_;
  std::unique_ptr<CanvasImpl> canvas_;
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_APP_H_
