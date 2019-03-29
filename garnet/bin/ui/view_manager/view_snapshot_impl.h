// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_SNAPSHOT_IMPL_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_SNAPSHOT_IMPL_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include "garnet/bin/ui/view_manager/view_registry.h"
#include "src/lib/fxl/macros.h"

namespace view_manager {

// ViewSnapshot interface implementation.
class ViewSnapshotImpl : public ::fuchsia::ui::viewsv1::ViewSnapshot {
 public:
  explicit ViewSnapshotImpl(ViewRegistry* registry);
  ~ViewSnapshotImpl() override;

 private:
  // |fuchsia::ui::viewsv1::ViewSnapshot|
  void TakeSnapshot(uint64_t view_koid, TakeSnapshotCallback callback) override;

  ViewRegistry* registry_;  // Not owned.

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewSnapshotImpl);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_SNAPSHOT_IMPL_H_
