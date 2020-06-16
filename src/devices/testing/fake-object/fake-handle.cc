// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include <lib/fake-object/object.h>
#include <lib/zx/status.h>
#include <zircon/types.h>

#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>

zx::status<fbl::RefPtr<Object>> HandleTable::GetLocked(zx_handle_t handle) {
  zx::status status = HandleToIndex(handle);
  if (!status.is_ok() || status.value() >= handles_.size() || !handles_[status.value()]) {
    ftracef("handle = 0x%x, not found\n", handle);
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  auto& obj = handles_[status.value()];
  ftracef("handle = 0x%x, obj = %p, type = %u, index = %zu\n", handle, obj.get(), obj->type(),
          status.value());
  return zx::success(obj);
}

__EXPORT
zx::status<zx_handle_t> HandleTable::Add(fbl::RefPtr<Object> obj) {
  fbl::AutoLock guard(&lock_);
  __UNUSED auto* obj_ptr = obj.get();
  zx_handle_t handle = ZX_HANDLE_INVALID;
  bool found_slot = false;
  size_t idx = 0;

  for (size_t i = 0; i < handles_.size(); ++i) {
    if (!handles_[i]) {
      idx = i;
      handles_[idx] = std::move(obj);
      found_slot = true;
      break;
    }
  }

  if (!found_slot) {
    idx = handles_.size();
    handles_.push_back(std::move(obj));
  }
  handle = IndexToHandle(idx);
  ftracef("handle = 0x%x, obj = %p, type = %u, index = %zu\n", handle, obj_ptr, obj_ptr->type(),
          idx);
  return zx::success(handle);
}

__EXPORT
zx::status<> HandleTable::Remove(zx_handle_t handle) {
  zx::status status = HandleToIndex(handle);
  if (!status.is_ok()) {
    return status.take_error();
  }
  size_t idx = status.value();

  // Pull the object out of the handle table so that we can release the handle
  // table lock before running the object's dtor. This prevents issues like
  // deadlocks if the object asserts in its dtor as a test object may do.
  fbl::RefPtr<Object> obj;
  {
    fbl::AutoLock guard(&lock_);
    obj = std::move(handles_[idx]);
  }
  ftracef("handle = 0x%x, obj = %p, type = %u, index = %zu\n", handle, obj.get(), obj->type(), idx);
  obj.reset();
  return zx::ok();
}

__EXPORT
void HandleTable::Clear() {
  fbl::AutoLock lock(&lock_);
  for (auto& e : handles_) {
    if (e) {
      e.reset();
    }
  }
}

__EXPORT
void HandleTable::Dump() {
  fbl::AutoLock lock(&lock_);
  int pos = 0;
  printf("Fake Handle Table [size: %zu]:\n", size_locked());
  for (auto& e : handles_) {
    printf("[%d] %p", pos++, e.get());
    if (e) {
      printf(" (type: %u)", static_cast<uint32_t>(e->type()));
    }
    printf("\n");
  }
}
