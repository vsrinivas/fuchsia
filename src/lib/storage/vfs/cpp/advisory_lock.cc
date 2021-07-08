// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/advisory_lock.h"

#include <lib/fidl/llcpp/object_view.h>

namespace fs {

namespace internal {

void advisory_lock(zx_koid_t owner, fbl::RefPtr<fs::Vnode> vnode, bool range_ok,
                   ::fuchsia_io2::wire::AdvisoryLockRequest& request,
                   fit::callback<void(zx_status_t status)> callback) {
  if (owner == ZX_KOID_INVALID) {
    callback(ZX_ERR_INTERNAL);
    return;
  }
  if (!request.has_type()) {
    callback(ZX_ERR_INVALID_ARGS);
    return;
  }
  file_lock::LockType lock_type;
  switch (request.type()) {
    case fuchsia_io2::wire::AdvisoryLockType::kRead:
      lock_type = file_lock::LockType::READ;
      break;
    case fuchsia_io2::wire::AdvisoryLockType::kWrite:
      lock_type = file_lock::LockType::WRITE;
      break;
    case fuchsia_io2::wire::AdvisoryLockType::kUnlock:
      lock_type = file_lock::LockType::UNLOCK;
      break;
  }

  // TODO(fxb/71330) implement range locking
  // for F_SETLK, F_SETLKW, and F_GETLK fcntl operations
  if (request.has_range()) {
    callback(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  auto lock = vnode->GetVnodeFileLock();
  if (lock == nullptr) {
    callback(ZX_ERR_INTERNAL);
    return;
  }
  bool wait = request.has_wait() ? request.wait() : false;
  file_lock::LockRequest req(lock_type, wait);
  lock->Lock(owner, req, callback);
}

}  // namespace internal

}  // namespace fs
