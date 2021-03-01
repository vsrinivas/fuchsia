// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_HUB_COMPONENT_HUB_H_
#define SRC_SYS_APPMGR_HUB_COMPONENT_HUB_H_

#include <zircon/types.h>

#include <fbl/ref_ptr.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/sys/appmgr/hub/hub.h"

namespace component {

class ComponentHub : public Hub {
 public:
  ComponentHub(fbl::RefPtr<fs::PseudoDir> root);

  zx_status_t SetProcessId(fbl::String koid) { return AddEntry("process-id", std::move(koid)); }

  zx_status_t SetArgs(fbl::String args) { return AddEntry("args", std::move(args)); }

  zx_status_t PublishOut(fbl::RefPtr<fs::Vnode> vn) { return AddEntry("out", std::move(vn)); }

  // Add a list of incoming services that the component has access to.
  zx_status_t AddIncomingServices(fbl::RefPtr<fs::Vnode> incoming_services);

  // Add a handle to the component's package
  zx_status_t AddPackageHandle(fbl::RefPtr<fs::Vnode> package_handle);

  ~ComponentHub();

 private:
  ComponentHub();

  // Initialize |in_dir_| if it hasn't already been initialized.
  zx_status_t EnsureInDir();

  // "in" directory
  fbl::RefPtr<fs::PseudoDir> in_dir_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentHub);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_HUB_COMPONENT_HUB_H_
