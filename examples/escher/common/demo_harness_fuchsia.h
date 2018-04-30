// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_FUCHSIA_H_
#define GARNET_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_FUCHSIA_H_

#include <memory>

#include <lib/async-loop/cpp/loop.h>

#include "garnet/examples/escher/common/demo_harness.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"

class DemoHarnessFuchsia : public DemoHarness {
 public:
  DemoHarnessFuchsia(async::Loop* loop, WindowParams window_params);

  // |DemoHarness|
  void Run(Demo* demo) override;

  fuchsia::sys::StartupContext* startup_context() {
    return startup_context_.get();
  }

 private:
  // Called by Init().
  void InitWindowSystem() override;
  vk::SurfaceKHR CreateWindowAndSurface(
      const WindowParams& window_params) override;

  // Called by Init() via CreateInstance().
  void AppendPlatformSpecificInstanceExtensionNames(
      InstanceParams* params) override;

  // Called by Shutdown().
  void ShutdownWindowSystem() override;

  void RenderFrameOrQuit();

  // DemoHarnessFuchsia can work with a pre-existing message loop, and also
  // create its own if necessary.
  async::Loop* loop_;
  std::unique_ptr<async::Loop> owned_loop_;

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
};

#endif  // GARNET_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_FUCHSIA_H_
