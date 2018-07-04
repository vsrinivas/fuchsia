// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/util.h"

#include <lib/zx/vmo.h>

#include "gtest/gtest.h"

namespace scenic {
namespace gfx {
namespace test {

bool IsEventSignalled(const zx::event& fence, zx_signals_t signal) {
  zx_signals_t pending = 0u;
  fence.wait_one(signal, zx::time(), &pending);
  return (pending & signal) != 0u;
}

zx::event CopyEvent(const zx::event& event) {
  zx::event event_copy;
  if (event.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_copy) != ZX_OK)
    FXL_LOG(ERROR) << "Copying zx::event failed.";
  return event_copy;
}

fidl::VectorPtr<zx::event> CopyEventIntoFidlArray(const zx::event& event) {
  auto event_array = fidl::VectorPtr<zx::event>::New(1);
  event_array->at(0) = CopyEvent(event);
  return event_array;
}

zx::eventpair CopyEventPair(const zx::eventpair& eventpair) {
  zx::eventpair eventpair_copy;
  if (eventpair.duplicate(ZX_RIGHT_SAME_RIGHTS, &eventpair_copy) != ZX_OK)
    FXL_LOG(ERROR) << "Copying zx::eventpair failed.";
  return eventpair_copy;
}

zx::vmo CopyVmo(const zx::vmo& vmo) {
  zx::vmo vmo_copy;
  if (vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy) != ZX_OK)
    FXL_LOG(ERROR) << "Copying zx::vmo failed.";
  return vmo_copy;
}

zx::event CreateEvent() {
  zx::event event;
  FXL_CHECK(zx::event::create(0, &event) == ZX_OK);
  return event;
}

fidl::VectorPtr<zx::event> CreateEventArray(size_t n) {
  ::fidl::VectorPtr<zx::event> events;
  for (size_t i = 0; i < n; i++) {
    events.push_back(CreateEvent());
  }
  return events;
}

fxl::RefPtr<fsl::SharedVmo> CreateSharedVmo(size_t size) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0u, &vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create vmo: status=" << status
                   << ", size=" << size;
    return nullptr;
  }

  // Optimization: We will be writing to every page of the buffer, so
  // allocate physical memory for it eagerly.
  status = vmo.op_range(ZX_VMO_OP_COMMIT, 0u, size, nullptr, 0u);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to commit all pages of vmo: status=" << status
                   << ", size=" << size;
    return nullptr;
  }

  uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
  return fxl::MakeRefCounted<fsl::SharedVmo>(std::move(vmo), map_flags);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic
