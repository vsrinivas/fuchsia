// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_HUB_HOLDER_H_
#define GARNET_BIN_APPMGR_HUB_HOLDER_H_

#include "lib/fxl/macros.h"

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <fs/vnode.h>

namespace component {

class HubHolder {
 public:
  HubHolder(fbl::RefPtr<fs::PseudoDir> root);

  const fbl::RefPtr<fs::PseudoDir>& root_dir() const { return root_dir_; }

  zx_status_t AddEntry(fbl::String name, fbl::RefPtr<fs::Vnode> vn);

  zx_status_t AddEntry(fbl::String name, fbl::String value);

  zx_status_t SetName(fbl::String name) {
    return AddEntry("name", fbl::move(name));
  }

  zx_status_t SetJobId(fbl::String koid) {
    return AddEntry("job-id", fbl::move(koid));
  }

 protected:
  fbl::RefPtr<fs::PseudoDir> root_dir_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(HubHolder);
};
}  // namespace component

#endif  // GARNET_BIN_APPMGR_HUB_HOLDER_H_
