// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "examples/common/demo_harness.h"

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
