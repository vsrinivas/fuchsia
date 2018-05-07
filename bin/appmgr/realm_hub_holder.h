// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_REALM_HUB_HOLDER_H_
#define GARNET_BIN_APPMGR_REALM_HUB_HOLDER_H_

#include "lib/fxl/macros.h"

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <zircon/types.h>

namespace component {

class Realm;
class ApplicationControllerImpl;

// TODO: refactor to also create ComponentHubHolder
class RealmHubHolder {
 public:
  RealmHubHolder(fbl::RefPtr<fs::PseudoDir> root);

  zx_status_t AddRealm(const Realm* realm);
  zx_status_t RemoveRealm(const Realm* realm);

  zx_status_t AddComponent(const ApplicationControllerImpl* application);
  zx_status_t RemoveComponent(const ApplicationControllerImpl* application);

  const fbl::RefPtr<fs::PseudoDir>& root_dir() const { return root_dir_; }

  ~RealmHubHolder() = default;

 private:
  fbl::RefPtr<fs::PseudoDir> root_dir_;
  fbl::RefPtr<fs::PseudoDir> realm_dir_;
  fbl::RefPtr<fs::PseudoDir> component_dir_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RealmHubHolder);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_REALM_HUB_HOLDER_H_
