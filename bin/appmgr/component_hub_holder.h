// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_COMPONENT_HUB_HOLDER_H_
#define GARNET_BIN_APPMGR_COMPONENT_HUB_HOLDER_H_

#include "garnet/bin/appmgr/hub_holder.h"

#include "lib/fxl/macros.h"

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <zircon/types.h>

namespace component {

class ComponentHubHolder : public HubHolder {
 public:
  ComponentHubHolder(fbl::RefPtr<fs::PseudoDir> root);

  zx_status_t SetProcessId(fbl::String koid) {
    return AddEntry("process-id", fbl::move(koid));
  }

  zx_status_t SetArgs(fbl::String args) {
    return AddEntry("args", fbl::move(args));
  }

  zx_status_t PublishOut(fbl::RefPtr<fs::Vnode> vn) {
    return AddEntry("out", fbl::move(vn));
  }

  ~ComponentHubHolder();

 private:
  ComponentHubHolder();

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentHubHolder);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_COMPONENT_HUB_HOLDER_H_
