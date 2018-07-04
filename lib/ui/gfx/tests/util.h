// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_UTIL_H_
#define GARNET_LIB_UI_GFX_TESTS_UTIL_H_

#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>

#include "lib/fidl/cpp/vector.h"
#include "lib/fsl/vmo/shared_vmo.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/time/time_delta.h"

namespace scenic {
namespace gfx {
namespace test {

// Synchronously checks whether the event has signalled any of the bits in
// |signal|.
bool IsEventSignalled(const zx::event& event, zx_signals_t signal);

// Create a duplicate of the event.
zx::event CopyEvent(const zx::event& event);

// Create a duplicate of the event, and create a new fidl Array of size one to
// wrap it.
fidl::VectorPtr<zx::event> CopyEventIntoFidlArray(const zx::event& event);

// Create a duplicate of the eventpair.
zx::eventpair CopyEventPair(const zx::eventpair& eventpair);

// Create a duplicate of the VMO.
zx::vmo CopyVmo(const zx::vmo& vmo);

// Create an event.
zx::event CreateEvent();

// Create a fidl::VectorPtr and populate with |n| newly created events.
fidl::VectorPtr<zx::event> CreateEventArray(size_t n);

// Creates a VMO with the specified size, immediately allocate physical memory
// for it, and wraps in a |fsl::SharedVmo| to make it easy to map it into the
// caller's address space.
fxl::RefPtr<fsl::SharedVmo> CreateSharedVmo(size_t size);

}  // namespace test
}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_TESTS_UTIL_H_
