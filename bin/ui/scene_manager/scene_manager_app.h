// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/app/cpp/application_context.h"
#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/ui/scenic/fidl/scene_manager.fidl.h"
#include "garnet/bin/ui/scene_manager/displays/display_manager.h"
#include "garnet/bin/ui/scene_manager/scene_manager_impl.h"
#include "lib/escher/examples/common/demo.h"
#include "lib/escher/examples/common/demo_harness_fuchsia.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"

namespace scene_manager {

// TODO(MZ-142): SceneManagerApp should not rely on escher::DemoHarness.
class SceneManagerApp {
 public:
  class Params {
   public:
    bool Setup(const ftl::CommandLine& command_line) { return true; }
  };

  SceneManagerApp(app::ApplicationContext* app_context,
                  Params* params,
                  DisplayManager* display_manager,
                  std::unique_ptr<DemoHarness> harness);
  ~SceneManagerApp();

 private:
  app::ApplicationContext* const application_context_;

  std::unique_ptr<DemoHarness> demo_harness_;
  escher::Escher escher_;
  std::unique_ptr<SceneManagerImpl> scene_manager_;

  fidl::BindingSet<scenic::SceneManager> bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SceneManagerApp);
};

}  // namespace scene_manager
