// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_HUB_HUB_H_
#define SRC_SYS_APPMGR_HUB_HUB_H_

#include <fbl/ref_ptr.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/vnode.h"
#include "src/sys/appmgr/hub/hub_info.h"

namespace component {

class Hub {
 public:
  Hub(fbl::RefPtr<fs::PseudoDir> root);

  const fbl::RefPtr<fs::PseudoDir>& dir() const { return dir_; }

  zx_status_t AddEntry(fbl::String name, fbl::RefPtr<fs::Vnode> vn);

  zx_status_t AddEntry(fbl::String name, fbl::String value);

  zx_status_t SetName(fbl::String name) { return AddEntry("name", std::move(name)); }

  zx_status_t SetJobId(fbl::String koid) { return AddEntry("job-id", std::move(koid)); }

  zx_status_t EnsureComponentDir();
  zx_status_t AddComponent(const HubInfo& hub_info);
  zx_status_t RemoveComponent(const HubInfo& hub_info);

 protected:
  fbl::RefPtr<fs::PseudoDir> dir_;
  fbl::RefPtr<fs::PseudoDir> component_dir_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Hub);
};
}  // namespace component

#endif  // SRC_SYS_APPMGR_HUB_HUB_H_
