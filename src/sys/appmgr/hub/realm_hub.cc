// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/hub/realm_hub.h"

#include <zircon/types.h>

#include <fbl/ref_ptr.h>
#include <fs/pseudo_dir.h>

#include "src/sys/appmgr/hub/hub.h"
#include "src/sys/appmgr/hub/hub_info.h"

namespace component {

RealmHub::RealmHub(fbl::RefPtr<fs::PseudoDir> root)
    : Hub(root), realm_dir_(fbl::AdoptRef(new fs::PseudoDir())) {
  AddEntry("r", realm_dir_);
  EnsureComponentDir();
}

zx_status_t RealmHub::AddRealm(const HubInfo& hub_info) {
  fbl::RefPtr<fs::Vnode> realm_instance_vnode;
  fbl::RefPtr<fs::PseudoDir> realm_instance_dir;
  zx_status_t status = realm_dir_->Lookup(hub_info.label(), &realm_instance_vnode);
  if (status == ZX_ERR_NOT_FOUND) {
    realm_instance_dir = fbl::AdoptRef(new fs::PseudoDir());
    realm_dir_->AddEntry(hub_info.label(), realm_instance_dir);
  } else {
    realm_instance_dir =
        fbl::RefPtr<fs::PseudoDir>(static_cast<fs::PseudoDir*>(realm_instance_vnode.get()));
  }
  return realm_instance_dir->AddEntry(hub_info.koid(), hub_info.hub_dir());
}

zx_status_t RealmHub::RemoveRealm(const HubInfo& hub_info) {
  fbl::RefPtr<fs::Vnode> realm_instance_vnode;
  zx_status_t status = realm_dir_->Lookup(hub_info.label(), &realm_instance_vnode);
  if (status == ZX_OK) {
    auto realm_instance_dir =
        fbl::RefPtr<fs::PseudoDir>(static_cast<fs::PseudoDir*>(realm_instance_vnode.get()));
    status = realm_instance_dir->RemoveEntry(hub_info.koid());
    if (realm_instance_dir->IsEmpty()) {
      realm_dir_->RemoveEntry(hub_info.label());
    }
    return status;
  }
  return ZX_ERR_NOT_FOUND;
}

RealmHub::~RealmHub() = default;

}  // namespace component
