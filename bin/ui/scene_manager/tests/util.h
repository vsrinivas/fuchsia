// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/event.h>

#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fsl/vmo/shared_vmo.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/time/time_delta.h"
#include "zircon/system/ulib/zx/include/zx/event.h"
#include "zircon/system/ulib/zx/include/zx/eventpair.h"

namespace scene_manager {
namespace test {

// How long to run the message loop when we want to allow a task in the
// task queue to run.
constexpr fxl::TimeDelta kPumpMessageLoopDuration =
    fxl::TimeDelta::FromMilliseconds(16);

// Synchronously checks whether the event has signalled any of the bits in
// |signal|.
bool IsEventSignalled(const zx::event& event, zx_signals_t signal);

// Create a duplicate of the event.
zx::event CopyEvent(const zx::event& event);

// Create a duplicate of the eventpair.
zx::eventpair CopyEventPair(const zx::eventpair& eventpair);

// Create a duplicate of the VMO.
zx::vmo CopyVmo(const zx::vmo& vmo);

// Create an event.
zx::event CreateEvent();

// Create a fidl::Array and populate with |n| newly created events.
fidl::Array<zx::event> CreateEventArray(size_t n);

// Creates a VMO with the specified size, immediately allocate physical memory
// for it, and wraps in a |fsl::SharedVmo| to make it easy to map it into the
// caller's address space.
fxl::RefPtr<fsl::SharedVmo> CreateSharedVmo(size_t size);

}  // namespace test
}  // namespace scene_manager
