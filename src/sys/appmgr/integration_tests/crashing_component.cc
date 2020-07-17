
// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/testing/appmgr/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <cstdlib>

namespace {

class CrashingComponent : public fuchsia::testing::appmgr::CrashInducer {
 public:
  void Crash() override { abort(); }
  fidl::InterfaceRequestHandler<fuchsia::testing::appmgr::CrashInducer> GetHandler(
      async_dispatcher_t* dispatcher) {
    return bindings_.GetHandler(this, dispatcher);
  }

  void AddBinding(zx::channel request, async_dispatcher_t* dispatcher) {
    bindings_.AddBinding(
        this, fidl::InterfaceRequest<fuchsia::testing::appmgr::CrashInducer>(std::move(request)),
        dispatcher);
  }

 private:
  fidl::BindingSet<fuchsia::testing::appmgr::CrashInducer> bindings_;
};

}  // namespace

int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  CrashingComponent component;
  context->outgoing()->AddPublicService(component.GetHandler(loop.dispatcher()));
  loop.Run();
}
