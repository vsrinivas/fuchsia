// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/event.h>

#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/mtl/vmo/shared_vmo.h"
#include "magenta/system/ulib/mx/include/mx/event.h"
#include "magenta/system/ulib/mx/include/mx/eventpair.h"

namespace scene_manager {
namespace test {

// How long to run the message loop when we want to allow a task in the
// task queue to run.
constexpr fxl::TimeDelta kPumpMessageLoopDuration =
    fxl::TimeDelta::FromMilliseconds(16);

// Synchronously checks whether the event has signalled any of the bits in
// |signal|.
bool IsEventSignalled(const mx::event& event, mx_signals_t signal);

// Create a duplicate of the event.
mx::event CopyEvent(const mx::event& event);

// Create a duplicate of the eventpair.
mx::eventpair CopyEventPair(const mx::eventpair& eventpair);

// Create a duplicate of the VMO.
mx::vmo CopyVmo(const mx::vmo& vmo);

// Creates a VMO with the specified size, immediately allocate physical memory
// for it, and wraps in a |mtl::SharedVmo| to make it easy to map it into the
// caller's address space.
fxl::RefPtr<mtl::SharedVmo> CreateSharedVmo(size_t size);

}  // namespace test
}  // namespace scene_manager
