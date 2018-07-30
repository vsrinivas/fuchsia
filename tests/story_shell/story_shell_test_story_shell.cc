// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the fuchsia::modular::StoryShell service that just lays out
// the views of all modules side by side.

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/story_shell/defs.h"

using modular::testing::Signal;

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
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::StoryContext>
                      story_context) override {
    story_context_.Bind(std::move(story_context));
    story_context_->GetPresentation(presentation_.NewRequest());
  }

  // Keep state to check ordering. Cf. below.
  bool seen_root_one_{};

  // |fuchsia::modular::StoryShell|
  void AddView(
      fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> view_owner,
      fidl::StringPtr view_id, fidl::StringPtr anchor_id,
      fuchsia::modular::SurfaceRelationPtr /*surface_relation*/,
      fuchsia::modular::ModuleManifestPtr module_manifest) override {
    FXL_LOG(INFO) << "AddView " << view_id << " " << anchor_id << " "
                  << (module_manifest ? module_manifest->composition_pattern
                                      : " NO MANIFEST");

    if (view_id == "root:one" && anchor_id == "root") {
      Signal("root:one");

      if (module_manifest && module_manifest->composition_pattern == "ticker" &&
          module_manifest->action == "com.google.fuchsia.common.null") {
        Signal("root:one manifest");
      }

      seen_root_one_ = true;
    }

    if (view_id == "root:one:two" && anchor_id == "root:one") {
      Signal("root:one:two");

      if (module_manifest && module_manifest->composition_pattern == "ticker" &&
          module_manifest->action == "com.google.fuchsia.common.null") {
        Signal("root:one:two manifest");
      }

      if (seen_root_one_) {
        Signal("root:one:two ordering");
      }
    }
  }

  // |fuchsia::modular::StoryShell|
  void FocusView(fidl::StringPtr /*view_id*/,
                 fidl::StringPtr /*relative_view_id*/) override {}

  // |fuchsia::modular::StoryShell|
  void DefocusView(fidl::StringPtr /*view_id*/,
                   DefocusViewCallback callback) override {
    callback();
  }

  // |fuchsia::modular::StoryShell|
  void AddContainer(
      fidl::StringPtr /*container_name*/, fidl::StringPtr /*parent_id*/,
      fuchsia::modular::SurfaceRelation /*relation*/,
      fidl::VectorPtr<fuchsia::modular::ContainerLayout> /*layout*/,
      fidl::VectorPtr<
          fuchsia::modular::ContainerRelationEntry> /* relationships */,
      fidl::VectorPtr<fuchsia::modular::ContainerView> /* views */) override {}

  fuchsia::modular::StoryContextPtr story_context_;
  fuchsia::ui::policy::PresentationPtr presentation_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /* argc */, const char** /* argv */) {
  FXL_LOG(INFO) << "Story Shell main";
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
