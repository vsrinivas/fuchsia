// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the fuchsia::modular::StoryShell service that just lays out
// the views of all modules side by side.

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/component/cpp/startup_context.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>

#include "peridot/lib/testing/component_base.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/embed_shell/defs.h"

using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp
    : public modular::testing::ComponentBase<fuchsia::modular::StoryShell> {
 public:
  TestApp(component::StartupContext* const startup_context)
      : ComponentBase(startup_context) {
    TestInit(__FILE__);
  }

  ~TestApp() override = default;

 private:
  // |fuchsia::modular::StoryShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::StoryShellContext>
                      story_shell_context) override {
    story_shell_context_.Bind(std::move(story_shell_context));
  }

  TestPoint add_surface_{"AddSurface root:child:child root"};

  // |fuchsia::modular::StoryShell|
  void AddSurface(fuchsia::modular::ViewConnection view_connection,
                  fuchsia::modular::SurfaceInfo surface_info) override {
    FXL_LOG(INFO) << "surface_id=" << view_connection.surface_id
                  << ", anchor_id=" << surface_info.parent_id;
    if (view_connection.surface_id == "root:child:child" &&
        surface_info.parent_id == "root") {
      add_surface_.Pass();
      modular::testing::GetStore()->Put("story_shell_done", "1", [] {});
    } else {
      FXL_LOG(WARNING) << "AddView " << view_connection.surface_id << " anchor "
                       << surface_info.parent_id;
    }
  }

  // |fuchsia::modular::StoryShell|
  void FocusSurface(std::string /*surface_id*/) override {}

  // |fuchsia::modular::StoryShell|
  void DefocusSurface(std::string /*surface_id*/,
                      DefocusSurfaceCallback callback) override {
    callback();
  }

  // |fuchsia::modular::StoryShell|
  void AddContainer(
      std::string /*container_name*/, fidl::StringPtr /*parent_id*/,
      fuchsia::modular::SurfaceRelation /*relation*/,
      std::vector<fuchsia::modular::ContainerLayout> /*layout*/,
      std::vector<fuchsia::modular::ContainerRelationEntry> /* relationships */,
      std::vector<fuchsia::modular::ContainerView> /* views */) override {}

  // |fuchsia::modular::StoryShell|
  void RemoveSurface(std::string /*surface_id*/) override {}

  // |fuchsia::modular::StoryShell|
  void ReconnectView(
      fuchsia::modular::ViewConnection view_connection) override {}

  // |fuchsia::modular::StoryShell|
  void UpdateSurface(fuchsia::modular::ViewConnection view_connection,
                     fuchsia::modular::SurfaceInfo /*surface_info*/) override{};

  fuchsia::modular::StoryShellContextPtr story_shell_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /* argc */, const char** /* argv */) {
  FXL_LOG(INFO) << "Embed Story Shell main";
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
