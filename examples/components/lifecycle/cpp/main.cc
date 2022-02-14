// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <zircon/processargs.h>

// [START imports]
#include <fuchsia/process/lifecycle/cpp/fidl.h>
// [END imports]

// [START lifecycle_handler]
// Implementation of the fuchsia.process.lifecycle FIDL protocol
class LifecycleManager : public fuchsia::process::lifecycle::Lifecycle {
 public:
  explicit LifecycleManager(async::Loop* loop) : loop_(loop) {
    // Get the PA_LIFECYCLE handle, and instantiate the channel with it
    zx::channel channel = zx::channel(zx_take_startup_handle(PA_LIFECYCLE));
    // Bind to the channel and start listening for events
    bindings_.AddBinding(
        this, fidl::InterfaceRequest<fuchsia::process::lifecycle::Lifecycle>(std::move(channel)),
        loop_->dispatcher());
    FX_LOGS(INFO) << "Lifecycle channel received." << std::endl;
  }

  // This is the Stop event we must override - see the pure virtual function we need to override at
  // the declaration of fuchsia::process::lifecycle::Lifecycle
  void Stop() override {
    FX_LOGS(INFO) << "Received request to stop, adios" << std::endl;
    // Shut down our loop - it's important to call Shutdown() here vs. Quit()
    loop_->Shutdown();
    // Close the binding
    bindings_.CloseAll();
  }

 private:
  async::Loop* loop_;
  fidl::BindingSet<fuchsia::process::lifecycle::Lifecycle> bindings_;
};
// [END lifecycle_handler]

int main(int argc, const char** argv) {
  // Create the main async event loop.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // Instantiate our class which gets the lifecycle channel and listens for stop events
  LifecycleManager lifecycle_handler(&loop);

  FX_LOGS(INFO) << "Awaiting request to close" << std::endl;

  // Run the loop (until it is shutdown by the Stop handler).
  loop.Run();
  return 0;
}
