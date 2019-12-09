// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_FUCHSIA_H_
#define SRC_UI_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_FUCHSIA_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include <trace-provider/provider.h>

#include "src/ui/examples/escher/common/demo_harness.h"
#include "src/ui/lib/input_reader/input_reader.h"

class DemoHarnessFuchsia : public DemoHarness, fuchsia::ui::input::InputDeviceRegistry {
 public:
  DemoHarnessFuchsia(async::Loop* loop, WindowParams window_params);

  sys::ComponentContext* component_context() { return component_context_.get(); }

 private:
  // |DemoHarness|
  // Called by Init().
  void InitWindowSystem() override;
  vk::SurfaceKHR CreateWindowAndSurface(const WindowParams& window_params) override;

  // |DemoHarness|
  // Called by Init() via CreateInstance().
  void AppendPlatformSpecificInstanceExtensionNames(InstanceParams* params) override;
  void AppendPlatformSpecificDeviceExtensionNames(std::set<std::string>* names) override;

  // |DemoHarness|
  // Called by Shutdown().
  void ShutdownWindowSystem() override;

  // |DemoHarness|
  // Called by Run().
  void RunForPlatform(Demo* demo) override;

  // |fuchsia::ui::input::InputDeviceRegistry|
  void RegisterDevice(
      fuchsia::ui::input::DeviceDescriptor descriptor,
      fidl::InterfaceRequest<fuchsia::ui::input::InputDevice> input_device) override;

  void RenderFrameOrQuit(Demo* demo);

  // DemoHarnessFuchsia can work with a pre-existing message loop, and also
  // create its own if necessary.
  async::Loop* loop_;
  std::unique_ptr<async::Loop> owned_loop_;
  trace::TraceProviderWithFdio trace_provider_;

  std::unique_ptr<sys::ComponentContext> component_context_;
  ui_input::InputReader input_reader_;
  fidl::BindingSet<fuchsia::ui::input::InputDevice,
                   std::unique_ptr<fuchsia::ui::input::InputDevice>>
      input_devices_;
};

#endif  // SRC_UI_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_FUCHSIA_H_
