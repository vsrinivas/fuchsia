// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_LINUX_H_
#define GARNET_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_LINUX_H_

#include "garnet/examples/escher/common/demo_harness.h"

class DemoHarnessLinux : public DemoHarness {
 public:
  DemoHarnessLinux(WindowParams window_params);

  void Run(Demo* demo) override;

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
};

#endif  // GARNET_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_LINUX_H_
