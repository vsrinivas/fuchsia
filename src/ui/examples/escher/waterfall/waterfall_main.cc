// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <cmath>

#include "src/ui/examples/escher/common/demo_harness.h"
#include "src/ui/examples/escher/waterfall/waterfall_demo.h"

std::unique_ptr<DemoHarness> CreateHarnessForDemo(std::string demo_name, uint32_t width,
                                                  uint32_t height, int argc, char** argv) {
  bool use_fullscreen = false;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp("--fullscreen", argv[i])) {
      use_fullscreen = true;
    }
  }

  DemoHarness::WindowParams window_params{demo_name, width, height, 2, use_fullscreen};

  DemoHarness::InstanceParams instance_params;
  auto validation_layer_name = escher::VulkanInstance::GetValidationLayerName();
  if (validation_layer_name) {
    instance_params.layer_names.insert(*validation_layer_name);
  }
  return DemoHarness::New(window_params, std::move(instance_params));
}

int main(int argc, char** argv) {
  auto harness = CreateHarnessForDemo("Escher Waterfall Demo", WaterfallDemo::kDemoWidth,
                                      WaterfallDemo::kDemoHeight, argc, argv);
  {
    WaterfallDemo demo(harness->escher()->GetWeakPtr(), argc, argv);
    harness->Run(&demo);
  }
  harness->Shutdown();
  return 0;
}
