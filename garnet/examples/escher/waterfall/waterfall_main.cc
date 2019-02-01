// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <cmath>

#include "garnet/examples/escher/common/demo_harness.h"
#include "garnet/examples/escher/waterfall/waterfall_demo.h"

std::unique_ptr<DemoHarness> CreateHarnessDemo(std::string demo_name,
                                               uint32_t width, uint32_t height,
                                               int argc, char** argv) {
  bool use_fullscreen = false;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp("--fullscreen", argv[i])) {
      use_fullscreen = true;
    }
  }

  DemoHarness::WindowParams window_params{demo_name, width, height, 2,
                                          use_fullscreen};

  return DemoHarness::New(window_params, DemoHarness::InstanceParams());
}

int main(int argc, char** argv) {
  auto harness =
      CreateHarnessDemo("Escher Waterfall Demo", WaterfallDemo::kDemoWidth,
                        WaterfallDemo::kDemoHeight, argc, argv);
  {
    WaterfallDemo demo(harness.get(), argc, argv);
    harness->Run(&demo);
  }
  harness->Shutdown();
  return 0;
}
