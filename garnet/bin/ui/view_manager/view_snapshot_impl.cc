// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_snapshot_impl.h"

#include "src/lib/fxl/logging.h"

namespace view_manager {

ViewSnapshotImpl::ViewSnapshotImpl(ViewRegistry* registry)
    : registry_(registry) {}

ViewSnapshotImpl::~ViewSnapshotImpl() {}

void ViewSnapshotImpl::TakeSnapshot(uint64_t view_koid,
                                    TakeSnapshotCallback callback) {
  registry_->TakeSnapshot(view_koid, std::move(callback));
}

}  // namespace view_manager
