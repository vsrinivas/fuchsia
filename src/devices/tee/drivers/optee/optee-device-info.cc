// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-device-info.h"

#include <cstring>

#include "optee-llcpp.h"

namespace optee {

zx_status_t OpteeDeviceInfo::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_tee::DeviceInfo::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void OpteeDeviceInfo::DdkRelease() { delete this; }

void OpteeDeviceInfo::GetOsInfo(GetOsInfoCompleter::Sync& completer) {
  OsRevision os_revision;
  os_revision.set_major(controller_->os_revision().major);
  os_revision.set_minor(controller_->os_revision().minor);

  OsInfo os_info;
  os_info.set_uuid(kOpteeOsUuid);
  os_info.set_revision(std::move(os_revision));
  os_info.set_is_global_platform_compliant(true);

  completer.Reply(os_info.to_llcpp());
}

}  // namespace optee
