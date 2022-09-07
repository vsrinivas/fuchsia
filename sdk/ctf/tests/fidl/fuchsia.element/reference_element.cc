// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <stdlib.h>

#include <iostream>
#include <utility>

class ViewProviderImpl : fuchsia::ui::app::ViewProvider {
 public:
  fidl::InterfaceRequestHandler<fuchsia::ui::app::ViewProvider> GetHandler() {
    return bindings_.GetHandler(this);
  }

 private:
  // |fuchsia.ui.app.ViewProvider|
  void CreateView(
      zx::eventpair view_handle,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> /*outgoing_services*/) override {
    std::cout << "WARNING: ViewControllerImpl.CreateView is not implemented" << std::endl;
  }

  // |fuchsia.ui.app.ViewProvider|
  void CreateViewWithViewRef(zx::eventpair view_handle,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override {
    std::cout << "WARNING: ViewControllerImpl.CreateViewWithViewRef is not implemented"
              << std::endl;
  }

  // |fuchsia.ui.app.ViewProvider|
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override {
    std::cout << "WARNING: ViewControllerImpl.CreateView2 is not implemented" << std::endl;
  }

  fidl::BindingSet<fuchsia::ui::app::ViewProvider> bindings_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  auto view_provider = std::make_unique<ViewProviderImpl>();
  context->outgoing()->AddPublicService(view_provider->GetHandler());

  loop.Run();
  return EXIT_SUCCESS;
}
