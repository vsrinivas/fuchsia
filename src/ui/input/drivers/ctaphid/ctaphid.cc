// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ctaphid.h"

#include <fbl/alloc_checker.h>

#include "src/ui/input/drivers/ctaphid/ctaphid_bind.h"

namespace ctaphid {

void CtapHidDriver::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void CtapHidDriver::DdkRelease() { delete this; }

void CtapHidDriver::SendMessage(SendMessageRequestView request,
                                SendMessageCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void CtapHidDriver::GetMessage(GetMessageRequestView request,
                               GetMessageCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

zx_status_t ctaphid_bind(void* ctx, zx_device_t* parent) {
  ddk::HidDeviceProtocolClient hiddev(parent);
  if (!hiddev.is_valid()) {
    return ZX_ERR_INTERNAL;
  }

  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<CtapHidDriver>(&ac, parent, hiddev);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->DdkAdd("SecurityKey");
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static zx_driver_ops_t ctaphid_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ctaphid_bind;
  return ops;
}();

}  // namespace ctaphid

ZIRCON_DRIVER(ctaphid, ctaphid::ctaphid_driver_ops, "zircon", "0.1");
