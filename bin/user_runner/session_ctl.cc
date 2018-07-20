// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/service.h>
#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/bin/user_runner/puppet_master/puppet_master_impl.h"
#include "peridot/bin/user_runner/session_ctl.h"

namespace modular {

// Directory layout:
//
// puppet_master       -   fuchsia::modular::PuppetMaster service
SessionCtl::SessionCtl(fbl::RefPtr<fs::PseudoDir> dir,
                       const std::string& entry_name,
                       PuppetMasterImpl* const puppet_master_impl)
    : dir_(dir),
      entry_name_(entry_name),
      puppet_master_impl_(puppet_master_impl) {
  auto ctl_dir = fbl::AdoptRef(new fs::PseudoDir());
  ctl_dir->AddEntry(
      "puppet_master",
      fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
        fidl::InterfaceRequest<fuchsia::modular::PuppetMaster> request(
            std::move(channel));
        puppet_master_impl_->Connect(std::move(request));
        return ZX_OK;
      })));

  auto status = dir_->AddEntry(entry_name_, ctl_dir);
  FXL_DCHECK(status == ZX_OK);
}

SessionCtl::~SessionCtl() {
  auto status = dir_->RemoveEntry(entry_name_);
  FXL_DCHECK(status == ZX_OK);
}

}  // namespace modular
