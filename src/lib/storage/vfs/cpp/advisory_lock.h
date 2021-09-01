// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_ADVISORY_LOCK_H_
#define SRC_LIB_STORAGE_VFS_CPP_ADVISORY_LOCK_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fidl/fuchsia.io/cpp/wire.h>

#include "lib/fit/function.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fs {

namespace internal {

void advisory_lock(zx_koid_t owner, fbl::RefPtr<fs::Vnode> vnode, bool range_ok,
                   ::fuchsia_io2::wire::AdvisoryLockRequest& request,
                   fit::callback<void(zx_status_t status)> callback);

}  // namespace internal

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_ADVISORY_LOCK_H_
