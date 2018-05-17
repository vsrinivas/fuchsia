// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_HUB_HUB_H_
#define GARNET_BIN_APPMGR_HUB_HUB_H_

#include "garnet/bin/appmgr/hub/hub_info.h"

#include "lib/fxl/macros.h"

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <fs/vnode.h>

namespace component {

class Hub {
 public:
  Hub(fbl::RefPtr<fs::PseudoDir> root);

  const fbl::RefPtr<fs::PseudoDir>& dir() const { return dir_; }

  zx_status_t AddEntry(fbl::String name, fbl::RefPtr<fs::Vnode> vn);

  zx_status_t AddEntry(fbl::String name, fbl::String value);

  zx_status_t SetName(fbl::String name) {
    return AddEntry("name", fbl::move(name));
  }

  zx_status_t SetJobId(fbl::String koid) {
    return AddEntry("job-id", fbl::move(koid));
  }

  zx_status_t AddComponent(const HubInfo& hub_info);
  zx_status_t RemoveComponent(const HubInfo& hub_info);

 protected:
  zx_status_t CreateComponentDir();

  fbl::RefPtr<fs::PseudoDir> dir_;
  fbl::RefPtr<fs::PseudoDir> component_dir_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Hub);
};
}  // namespace component

#endif  // GARNET_BIN_APPMGR_HUB_HUB_H_
