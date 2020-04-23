// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include <lib/fake-object/object.h>
#include <zircon/types.h>

#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>

__EXPORT
zx_status_t HandleTable::Get(zx_handle_t handle, fbl::RefPtr<Object>* out) {
  fbl::AutoLock guard(&lock_);

  size_t idx = HandleToIndex(handle);
  if (idx >= handles_.size()) {
    return ZX_ERR_NOT_FOUND;
  }
  const fbl::RefPtr<Object>& h = handles_[idx];
  if (!h) {
    return ZX_ERR_NOT_FOUND;
  }
  *out = h;
  return ZX_OK;
}

__EXPORT
zx_status_t HandleTable::Add(fbl::RefPtr<Object>&& obj, zx_handle_t* out) {
  fbl::AutoLock guard(&lock_);

  for (size_t i = 0; i < handles_.size(); ++i) {
    if (!handles_[i]) {
      handles_[i] = std::move(obj);
      *out = IndexToHandle(i);
      ftracef("fake_handle_%s: handle = %#x, type = %u, obj = %p, index = %zu\n", __func__, *out,
              static_cast<uint32_t>(handles_[i]->type()), handles_[i].get(), i);
      return ZX_OK;
    }
  }

  handles_.push_back(std::move(obj));
  *out = IndexToHandle(handles_.size() - 1);
  ftracef("fake_handle_%s: handle = %#x, type = %u, obj = %p, index = %zu\n", __func__, *out,
          static_cast<uint32_t>(handles_[handles_.size() - 1]->type()),
          handles_[handles_.size() - 1].get(), handles_.size() - 1);
  return ZX_OK;
}

__EXPORT
zx_status_t HandleTable::Remove(zx_handle_t handle) {
  fbl::AutoLock guard(&lock_);

  size_t idx = HandleToIndex(handle);
  if (idx >= handles_.size()) {
    return ZX_ERR_NOT_FOUND;
  }
  fbl::RefPtr<Object>* h = &handles_[idx];
  if (!*h) {
    return ZX_ERR_NOT_FOUND;
  }
  h->reset();
  return ZX_OK;
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
