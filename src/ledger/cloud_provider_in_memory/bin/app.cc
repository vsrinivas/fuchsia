// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "src/ledger/cloud_provider_in_memory/lib/fake_cloud_provider.h"

namespace cloud_provider {
namespace {

class App {
 public:
  explicit App(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher), component_context_(sys::ComponentContext::Create()) {
    FXL_DCHECK(component_context_);
  }
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  ~App() = default;

  bool Start() {
    cloud_provider_impl_ = std::make_unique<ledger::FakeCloudProvider>(dispatcher_);

    component_context_->outgoing()->AddPublicService<CloudProvider>(
        [this](fidl::InterfaceRequest<CloudProvider> request) {
          cloud_provider_bindings_.AddBinding(cloud_provider_impl_.get(), std::move(request));
        });

    return true;
  }

 private:
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  std::unique_ptr<ledger::FakeCloudProvider> cloud_provider_impl_;
  fidl::BindingSet<CloudProvider> cloud_provider_bindings_;
};

int Main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  App app(loop.dispatcher());
  int return_code = 0;
  async::PostTask(loop.dispatcher(), [&return_code, &app, &loop] {
    if (!app.Start()) {
      return_code = -1;
      loop.Quit();
    }
  });
  loop.Run();
  return return_code;
}

}  // namespace
}  // namespace cloud_provider

int main(int argc, const char** argv) { return cloud_provider::Main(argc, argv); }
