// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "demo.h"

#include "lib/fxl/logging.h"

Demo::Demo(DemoHarness* harness)
    : harness_(harness),
      vulkan_context_(harness->GetVulkanContext()),
      escher_(harness->device_queues(), harness->filesystem()) {}

Demo::~Demo() {}

bool Demo::HandleKeyPress(std::string key) {
  if (key.size() > 1) {
    if (key == "ESCAPE") {
      harness_->SetShouldQuit();
      return true;
    } else if (key == "SPACE") {
      return false;
    } else if (key == "RETURN") {
      return false;
    }
    // Illegal value.
    FXL_LOG(ERROR) << "Cannot handle key value: " << key;
    FXL_CHECK(false);
    return false;
  } else {
    char key_char = key[0];
    switch (key_char) {
      case 'T':
        ToggleTracing();
        return true;
      default:
        return false;
    }
  }
}

void Demo::ToggleTracing() {
#ifdef __fuchsia__
  // On Fuchsia, use system-wide tracing in the usual way.
  FXL_LOG(INFO) << "ToggleTracing() only supported for Escher-Linux.";
#else
  if (tracer_) {
    tracer_.reset();
    FXL_LOG(INFO) << "Tracing disabled.";
  } else {
    tracer_ = std::make_unique<escher::Tracer>();
    FXL_LOG(INFO) << "Tracing enabled.";
  }
#endif
}
