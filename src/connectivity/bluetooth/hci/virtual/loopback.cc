// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "loopback.h"

#include <fuchsia/hardware/bt/hci/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/assert.h>
#include <zircon/status.h>

namespace bt_hci_virtual {

zx_status_t LoopbackDevice::BtHciOpenCommandChannel(zx::channel channel) { return ZX_OK; }

zx_status_t LoopbackDevice::BtHciOpenAclDataChannel(zx::channel channel) { return ZX_OK; }

zx_status_t LoopbackDevice::BtHciOpenScoChannel(zx::channel channel) {
  return ZX_ERR_NOT_SUPPORTED;
}

void LoopbackDevice::BtHciConfigureSco(sco_coding_format_t coding_format, sco_encoding_t encoding,
                                       sco_sample_rate_t sample_rate,
                                       bt_hci_configure_sco_callback callback, void* cookie) {
  callback(cookie, ZX_ERR_NOT_SUPPORTED);
}

void LoopbackDevice::BtHciResetSco(bt_hci_reset_sco_callback callback, void* cookie) {}

zx_status_t LoopbackDevice::BtHciOpenSnoopChannel(zx::channel channel) { return ZX_OK; }

void LoopbackDevice::DdkUnbind(ddk::UnbindTxn txn) { zxlogf(TRACE, "Unbind"); }

void LoopbackDevice::DdkRelease() {
  zxlogf(TRACE, "Release");
  // Driver manager is given a raw pointer to this dynamically allocated object in Create(), so
  // when DdkRelease() is called we need to free the allocated memory.
  delete this;
}

zx_status_t LoopbackDevice::Bind(zx_handle_t channel) { return ZX_OK; }

}  // namespace bt_hci_virtual
