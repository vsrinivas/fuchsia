// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_FUCHSIA_UTILS_H_
#define LIB_ESCHER_UTIL_FUCHSIA_UTILS_H_

#include <lib/zx/event.h>
#include <lib/zx/vmo.h>

#include "lib/escher/escher.h"
#include "lib/escher/renderer/semaphore.h"

namespace escher {

// Create a new escher::Semaphore and a corresponding zx::event using
// the VK_KHR_EXTERNAL_SEMAPHORE_FD extension.  If it fails, both elements
// of the pair will be null.
std::pair<escher::SemaphorePtr, zx::event> NewSemaphoreEventPair(
    escher::Escher* escher);

// Exports a Semaphore into an event.
zx::event GetEventForSemaphore(
    const escher::VulkanDeviceQueues::ProcAddrs& proc_addresses,
    const vk::Device& device, const escher::SemaphorePtr& semaphore);

// Export the escher::GpuMem as a zx::vmo.
zx::vmo ExportMemoryAsVmo(escher::Escher* escher, const escher::GpuMemPtr& mem);

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_FUCHSIA_UTILS_H_
