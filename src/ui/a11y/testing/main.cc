// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <cstring>

#include "src/ui/a11y/lib/view/flatland_accessibility_view.h"
#include "src/ui/a11y/testing/fake_a11y_manager.h"

namespace {

int run_a11y_manager(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::Create();

  // For flatland scenes, we need to serve
  // `fuchsia.accessibility.scene.Provider`.
  //
  // First, query scenic to determine which composition API to use. Then, if
  // we're using flatland, create an accessibility view object.
  fuchsia::ui::scenic::ScenicSyncPtr scenic;
  context->svc()->Connect<fuchsia::ui::scenic::Scenic>(scenic.NewRequest());

  bool use_flatland = false;
  scenic->UsesFlatland(&use_flatland);

  std::unique_ptr<a11y::FlatlandAccessibilityView> maybe_a11y_view;
  if (use_flatland) {
    maybe_a11y_view = std::make_unique<a11y::FlatlandAccessibilityView>(
        context->svc()->Connect<fuchsia::ui::composition::Flatland>(),
        context->svc()->Connect<fuchsia::ui::composition::Flatland>(),
        context->svc()->Connect<fuchsia::ui::observation::scope::Registry>());
    context->outgoing()->AddPublicService(maybe_a11y_view->GetHandler());
  }

  a11y_testing::FakeA11yManager fake_a11y_manager;
  context->outgoing()->AddPublicService(fake_a11y_manager.GetHandler());

  a11y_testing::FakeMagnifier fake_magnifier;
  context->outgoing()->AddPublicService(fake_magnifier.GetTestMagnifierHandler());
  context->outgoing()->AddPublicService(fake_magnifier.GetMagnifierHandler());

  context->outgoing()->ServeFromStartupInfo();

  loop.Run();
  return 0;
}

}  // namespace

int main(int argc, const char** argv) { return run_a11y_manager(argc, argv); }
