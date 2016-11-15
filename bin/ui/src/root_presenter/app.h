// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_ROOT_PRESENTER_APP_H_
#define APPS_MOZART_SRC_ROOT_PRESENTER_APP_H_

#include <memory>
#include <vector>

#include "apps/modular/lib/app/application_context.h"
#include "apps/mozart/services/composition/compositor.fidl.h"
#include "apps/mozart/services/presentation/presenter.fidl.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace root_presenter {

class Presentation;

// The presenter provides a |mozart::Presenter| service which displays
// UI by attaching the provided view to the root of a new view tree
// associated with a new renderer.
//
// Any number of view trees can be created, although multi-display support
// and input routing is not fully supported (TODO).
class App : public mozart::Presenter {
 public:
  explicit App(const ftl::CommandLine& command_line);
  ~App();

 private:
  // |Presenter|:
  void Present(fidl::InterfaceHandle<mozart::ViewOwner> view_owner) override;

  void InitializeServices();
  void Reset();

  std::unique_ptr<modular::ApplicationContext> application_context_;
  fidl::BindingSet<Presenter> presenter_bindings_;

  mozart::CompositorPtr compositor_;
  mozart::ViewManagerPtr view_manager_;

  std::vector<std::unique_ptr<Presentation>> presentations_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace root_presenter

#endif  // APPS_MOZART_SRC_ROOT_PRESENTER_APP_H_
