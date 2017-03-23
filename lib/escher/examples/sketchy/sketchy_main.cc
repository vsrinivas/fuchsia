// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <cmath>

#include "escher/examples/common/demo_harness.h"
#include "escher/examples/sketchy/sketchy_demo.h"

std::unique_ptr<DemoHarness> CreateHarnessDemo(std::string demo_name,
                                               uint32_t width,
                                               uint32_t height,
                                               int argc,
                                               char** argv) {
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
      CreateHarnessDemo("Escher Sketchy Demo", SketchyDemo::kDemoWidth,
                        SketchyDemo::kDemoHeight, argc, argv);
  {
    SketchyDemo demo(harness.get(), argc, argv);
    harness->Run(&demo);
  }
  harness->Shutdown();
  return 0;
}
