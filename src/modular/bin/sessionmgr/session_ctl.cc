// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/session_ctl.h"

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/vfs/cpp/service.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/bin/sessionmgr/puppet_master/puppet_master_impl.h"

namespace modular {

// Directory layout:
//
// puppet_master       -   fuchsia::modular::PuppetMaster service
SessionCtl::SessionCtl(vfs::PseudoDir* dir, const std::string& entry_name,
                       PuppetMasterImpl* const puppet_master_impl)
    : dir_(dir), entry_name_(entry_name), puppet_master_impl_(puppet_master_impl) {
  auto ctl_dir = std::make_unique<vfs::PseudoDir>();
  ctl_dir->AddEntry(
      fuchsia::modular::PuppetMaster::Name_,
      std::make_unique<vfs::Service>([this](zx::channel channel, async_dispatcher_t*) {
        fidl::InterfaceRequest<fuchsia::modular::PuppetMaster> request(std::move(channel));
        puppet_master_impl_->Connect(std::move(request));
      }));

  auto status = dir_->AddEntry(entry_name_, std::move(ctl_dir));
  FX_DCHECK(status == ZX_OK);
}

SessionCtl::~SessionCtl() {
  auto status = dir_->RemoveEntry(entry_name_);
  FX_DCHECK(status == ZX_OK);
}

}  // namespace modular
