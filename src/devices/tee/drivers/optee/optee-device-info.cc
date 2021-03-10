// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-device-info.h"

#include <cstring>

namespace optee {

zx_status_t OpteeDeviceInfo::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_tee::DeviceInfo::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void OpteeDeviceInfo::DdkRelease() { delete this; }

void OpteeDeviceInfo::GetOsInfo(GetOsInfoCompleter::Sync& completer) {
  fidl::FidlAllocator allocator;
  fuchsia_tee::wire::OsRevision os_revision(allocator);
  os_revision.set_major(allocator, controller_->os_revision().major);
  os_revision.set_minor(allocator, controller_->os_revision().minor);

  fuchsia_tee::wire::OsInfo os_info(allocator);
  os_info.set_uuid(allocator, kOpteeOsUuid);
  os_info.set_revision(allocator, std::move(os_revision));
  os_info.set_is_global_platform_compliant(allocator, true);

  completer.Reply(std::move(os_info));
}

}  // namespace optee
