// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <stdlib.h>

#include <utility>

// TODO(fxbug.dev/89476): Remove and replace with real Scenic when available.
class ScenicImpl : public fuchsia::ui::scenic::Scenic {
 public:
  fidl::InterfaceRequestHandler<fuchsia::ui::scenic::Scenic> GetHandler() {
    return bindings_.GetHandler(this);
  }

 private:
  // |fuchsia::ui::scenic::Scenic|
  void CreateSession(
      fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
      fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) override {}
  // |fuchsia::ui::scenic::Scenic|
  void CreateSession2(fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
                      fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener,
                      fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser) override {}
  // |fuchsia::ui::scenic::Scenic|
  void CreateSessionT(fuchsia::ui::scenic::SessionEndpoints endpoints,
                      CreateSessionTCallback callback) override {}
  // |fuchsia::ui::scenic::Scenic|
  void GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) override {}
  // |fuchsia::ui::scenic::Scenic|
  void TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) override {}
  // |fuchsia::ui::scenic::Scenic|
  void GetDisplayOwnershipEvent(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) override {}
  // |fuchsia::ui::scenic::Scenic|
  void UsesFlatland(fuchsia::ui::scenic::Scenic::UsesFlatlandCallback callback) override {
    callback(true);
  }

  fidl::BindingSet<fuchsia::ui::scenic::Scenic> bindings_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  auto scenic = std::make_unique<ScenicImpl>();
  context->outgoing()->AddPublicService(scenic->GetHandler());

  loop.Run();
  return EXIT_SUCCESS;
}
