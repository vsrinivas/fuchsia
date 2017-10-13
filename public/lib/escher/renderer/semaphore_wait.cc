// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/semaphore_wait.h"

#include "escher/impl/vulkan_utils.h"

namespace escher {

Semaphore::Semaphore(vk::Device device) : device_(device) {
  vk::SemaphoreCreateInfo info;
  value_ = ESCHER_CHECKED_VK_RESULT(device_.createSemaphore(info));
}

Semaphore::~Semaphore() {
  device_.destroySemaphore(value_);
}

SemaphorePtr Semaphore::New(vk::Device device) {
  return fxl::MakeRefCounted<Semaphore>(device);
}

}  // namespace escher
