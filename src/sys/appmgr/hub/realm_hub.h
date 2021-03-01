// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_HUB_REALM_HUB_H_
#define SRC_SYS_APPMGR_HUB_REALM_HUB_H_

#include <zircon/types.h>

#include <fbl/ref_ptr.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/vnode.h"
#include "src/sys/appmgr/hub/hub.h"
#include "src/sys/appmgr/hub/hub_info.h"

namespace component {

class Realm;
class HubInfo;

// TODO: refactor to also create ComponentHub
class RealmHub : public Hub {
 public:
  RealmHub(fbl::RefPtr<fs::PseudoDir> root);

  zx_status_t AddRealm(const HubInfo& hub_info);
  zx_status_t RemoveRealm(const HubInfo& hub_info);

  zx_status_t AddServices(fbl::RefPtr<fs::Vnode> svc) { return AddEntry("svc", std::move(svc)); }

  zx_status_t AddJobProvider(fbl::RefPtr<fs::Service> job_provider) {
    return AddEntry("job", std::move(job_provider));
  }

  ~RealmHub();

 private:
  fbl::RefPtr<fs::PseudoDir> realm_dir_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RealmHub);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_HUB_REALM_HUB_H_
