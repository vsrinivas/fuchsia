// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(MZ-258): the utilities in this file should be moved somewhere more
// generally useful, perhaps to a new escher_fuschia library.

#pragma once

#include <mx/event.h>
#include <mx/vmo.h>

#include "escher/escher.h"
#include "escher/renderer/semaphore_wait.h"

namespace shadertoy {

// Create a new escher::Semaphore and a corresponding mx::event using
// the VK_KHR_EXTERNAL_SEMAPHORE_FD extension.  If it fails, both elements
// of the pair will be null.
std::pair<escher::SemaphorePtr, mx::event> NewSemaphoreEventPair(
    escher::Escher* escher);

// Export the escher::GpuMem as a mx::vmo.
mx::vmo ExportMemoryAsVMO(escher::Escher* escher, const escher::GpuMemPtr& mem);

}  // namespace shadertoy
