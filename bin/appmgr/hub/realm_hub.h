// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_HUB_REALM_HUB_H_
#define GARNET_BIN_APPMGR_HUB_REALM_HUB_H_

#include "garnet/bin/appmgr/hub/hub.h"
#include "garnet/bin/appmgr/hub/hub_info.h"

#include "lib/fxl/macros.h"

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/vnode.h>
#include <zircon/types.h>

namespace component {

class Realm;
class HubInfo;

// TODO: refactor to also create ComponentHub
class RealmHub : public Hub {
 public:
  RealmHub(fbl::RefPtr<fs::PseudoDir> root);

  zx_status_t AddRealm(const HubInfo& hub_info);
  zx_status_t RemoveRealm(const HubInfo& hub_info);

  zx_status_t AddServices(fbl::RefPtr<fs::Vnode> svc) {
    return AddEntry("svc", fbl::move(svc));
  }

  zx_status_t AddJobProvider(fbl::RefPtr<fs::Service> job_provider) {
    return AddEntry("job", fbl::move(job_provider));
  }

  ~RealmHub();

 private:
  fbl::RefPtr<fs::PseudoDir> realm_dir_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RealmHub);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_HUB_REALM_HUB_H_
