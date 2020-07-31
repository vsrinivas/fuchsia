// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/process/lifecycle/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

class TestLifecycleComponent : public fuchsia::process::lifecycle::Lifecycle {
 public:
  explicit TestLifecycleComponent(std::shared_ptr<sys::OutgoingDirectory> outgoing_services,
                                  fit::function<void()> on_stop)
      : on_stop_(std::move(on_stop)) {
    outgoing_services->AddPublicService(lifecycle_bindings_.GetHandler(this),
                                        "fuchsia.process.lifecycle.Lifecycle");
  }

  ~TestLifecycleComponent() = default;

  // |fuchsia::process::lifecycle::Lifecycle|
  void Stop() override {
    FX_LOGS(INFO) << "Test Component Stop Called";
    // Sleeping for 50ms to test that appmgr actually waits for the component to stop before
    // shutting itself down.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
    on_stop_();
  }

 private:
  fidl::BindingSet<fuchsia::process::lifecycle::Lifecycle> lifecycle_bindings_;
  fit::function<void()> on_stop_;
};

int main(int argc, const char** argv) {
  syslog::SetTags({"test_lifecycle_component"});

  FX_LOGS(INFO) << "Launching TestLifecycleComponent";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::unique_ptr<sys::ComponentContext> component_context(
      sys::ComponentContext::CreateAndServeOutgoingDirectory());
  TestLifecycleComponent test_lifecycle_component(component_context->outgoing(),
                                                  [&loop]() { loop.Quit(); });

  loop.Run();

  // The loop will run until graceful shutdown is complete so returning SUCCESS here indicates that.
  return EXIT_SUCCESS;
}
