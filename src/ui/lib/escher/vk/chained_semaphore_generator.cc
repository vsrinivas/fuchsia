// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/chained_semaphore_generator.h"

#include "src/ui/lib/escher/renderer/semaphore.h"

#include <vulkan/vulkan.hpp>

namespace escher {

ChainedSemaphoreGenerator::ChainedSemaphoreGenerator(vk::Device device)
    : device_(device), weak_factory_(this) {}

SemaphorePtr ChainedSemaphoreGenerator::CreateNextSemaphore(bool exportable) {
  FXL_DCHECK(!last_semaphore_);
#ifndef __Fuchsia__
  FXL_DCHECK(!exportable) << "exportable semaphore is not supported on this platform";
#endif
  last_semaphore_ = exportable ? Semaphore::NewExportableSem(device_) : Semaphore::New(device_);
  return last_semaphore_;
};

SemaphorePtr ChainedSemaphoreGenerator::TakeLastSemaphore() { return std::move(last_semaphore_); }

}  // namespace escher
