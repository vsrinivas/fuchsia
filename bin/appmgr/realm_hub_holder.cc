// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/realm_hub_holder.h"
#include "garnet/bin/appmgr/hub_holder.h"

#include "garnet/bin/appmgr/application_controller_impl.h"
#include "garnet/bin/appmgr/realm.h"

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <zircon/types.h>

namespace component {

RealmHubHolder::RealmHubHolder(fbl::RefPtr<fs::PseudoDir> root)
    : HubHolder(root) {}

zx_status_t RealmHubHolder::AddRealm(const Realm* realm) {
  bool child_found = false;
  fbl::RefPtr<fs::Vnode> realm_instance_vnode;
  fbl::RefPtr<fs::PseudoDir> realm_instance_dir;
  if (!realm_dir_) {
    realm_dir_ = fbl::AdoptRef(new fs::PseudoDir());
    AddEntry("r", realm_dir_);
  } else {
    zx_status_t status =
        realm_dir_->Lookup(&realm_instance_vnode, realm->label());
    child_found = (status != ZX_ERR_NOT_FOUND);
  }
  if (!child_found) {
    realm_instance_dir = fbl::AdoptRef(new fs::PseudoDir());
    realm_dir_->AddEntry(realm->label(), realm_instance_dir);
  } else {
    realm_instance_dir = fbl::RefPtr<fs::PseudoDir>(
        static_cast<fs::PseudoDir*>(realm_instance_vnode.get()));
  }
  return realm_instance_dir->AddEntry(realm->koid(), realm->hub_dir());
}

zx_status_t RealmHubHolder::RemoveRealm(const Realm* realm) {
  fbl::RefPtr<fs::Vnode> realm_instance_vnode;
  zx_status_t status =
      realm_dir_->Lookup(&realm_instance_vnode, realm->label());
  if (status == ZX_OK) {
    auto realm_instance_dir = fbl::RefPtr<fs::PseudoDir>(
        static_cast<fs::PseudoDir*>(realm_instance_vnode.get()));
    status = realm_instance_dir->RemoveEntry(realm->koid());
    if (realm_instance_dir->IsEmpty()) {
      realm_dir_->RemoveEntry(realm->label());
    }
    return status;
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t RealmHubHolder::AddComponent(
    const ApplicationControllerImpl* application) {
  bool child_found = false;
  fbl::RefPtr<fs::PseudoDir> component_instance_dir;
  fbl::RefPtr<fs::Vnode> component_instance_vnode;
  if (!component_dir_) {
    component_dir_ = fbl::AdoptRef(new fs::PseudoDir());
    AddEntry("c", component_dir_);
  } else {
    zx_status_t status =
        component_dir_->Lookup(&component_instance_vnode, application->label());
    child_found = (status != ZX_ERR_NOT_FOUND);
  }
  if (!child_found) {
    component_instance_dir = fbl::AdoptRef(new fs::PseudoDir());
    component_dir_->AddEntry(application->label(), component_instance_dir);
  } else {
    component_instance_dir = fbl::RefPtr<fs::PseudoDir>(
        static_cast<fs::PseudoDir*>(component_instance_vnode.get()));
  }
  return component_instance_dir->AddEntry(application->koid(),
                                          application->hub_dir());
}

zx_status_t RealmHubHolder::RemoveComponent(
    const ApplicationControllerImpl* application) {
  fbl::RefPtr<fs::Vnode> component_instance_vnode;
  zx_status_t status =
      component_dir_->Lookup(&component_instance_vnode, application->label());
  if (status == ZX_OK) {
    auto component_instance_dir = fbl::RefPtr<fs::PseudoDir>(
        static_cast<fs::PseudoDir*>(component_instance_vnode.get()));
    status = component_instance_dir->RemoveEntry(application->koid());
    if (component_instance_dir->IsEmpty()) {
      component_dir_->RemoveEntry(application->label());
    }
    return status;
  }
  return ZX_ERR_NOT_FOUND;
}

RealmHubHolder::~RealmHubHolder() = default;

}  // namespace component
