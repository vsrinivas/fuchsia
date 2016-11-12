// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_LAUNCHER_APP_H_
#define APPS_MOZART_SRC_LAUNCHER_APP_H_

#include <memory>
#include <vector>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/services/application/application_controller.fidl.h"
#include "apps/mozart/services/composition/compositor.fidl.h"
#include "apps/mozart/services/launcher/launcher.fidl.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace launcher {

class Presenter;

// The launcher provides a |mozart::Launcher| service which displays
// UI by attaching the provided view to the root of a new view tree.
//
// If the launcher receives arguments on the command-line, then it
// will automatically launch the application described by those
// arguments.
//
// Any number of view trees can be created, although multi-display support
// and input routing is not fully supported (TODO).
class App : public mozart::Launcher {
 public:
  explicit App(const ftl::CommandLine& command_line);
  ~App();

 private:
  // |Launcher|:
  void Display(fidl::InterfaceHandle<mozart::ViewOwner> view_owner) override;

  void Launch(fidl::String url, fidl::Array<fidl::String> arguments);
  void InitializeServices();
  void Reset();

  std::unique_ptr<modular::ApplicationContext> application_context_;
  fidl::BindingSet<Launcher> launcher_bindings_;

  mozart::CompositorPtr compositor_;
  mozart::ViewManagerPtr view_manager_;

  modular::ApplicationControllerPtr app_controller_;

  std::vector<std::unique_ptr<Presenter>> presenters_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace launcher

#endif  // APPS_MOZART_SRC_LAUNCHER_APP_H_
