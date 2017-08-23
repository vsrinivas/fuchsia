// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "demo.h"

#include "lib/ftl/logging.h"

Demo::Demo(DemoHarness* harness)
    : harness_(harness),
      vulkan_context_(harness->GetVulkanContext()),
      escher_(harness->device_queues()) {}

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
    } else {
      // Illegal value.
      FTL_LOG(ERROR) << "Cannot handle key value: " << key;
      FTL_CHECK(false);
      return false;
    }
  }
  return false;
}
