// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_CTAPHID_CTAPHID_H_
#define SRC_UI_INPUT_DRIVERS_CTAPHID_CTAPHID_H_

#include <fidl/fuchsia.fido.report/cpp/wire.h>
#include <fuchsia/hardware/hiddevice/cpp/banjo.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace ctaphid {

class CtapHidDriver;
using CtapHidDriverDeviceType =
    ddk::Device<CtapHidDriver, ddk::Unbindable,
                ddk::Messageable<fuchsia_fido_report::SecurityKeyDevice>::Mixin>;
class CtapHidDriver : public CtapHidDriverDeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_CTAP> {
 public:
  CtapHidDriver(zx_device_t* parent, ddk::HidDeviceProtocolClient hiddev)
      : CtapHidDriverDeviceType(parent), hiddev_(hiddev) {}

  // DDK Functions.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // FIDL functions.
  void SendMessage(SendMessageRequestView request, SendMessageCompleter::Sync& completer) override;
  void GetMessage(GetMessageRequestView request, GetMessageCompleter::Sync& completer) override;

 private:
  ddk::HidDeviceProtocolClient hiddev_;
};

}  // namespace ctaphid

#endif  // SRC_UI_INPUT_DRIVERS_CTAPHID_CTAPHID_H_
