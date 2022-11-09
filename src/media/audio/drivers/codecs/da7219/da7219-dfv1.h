// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_DFV1_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_DFV1_H_

#include <ddktl/device.h>
#include <ddktl/fidl.h>

#include "da7219-server.h"

namespace audio::da7219 {

class Driver;
using Base = ddk::Device<Driver, ddk::Messageable<fuchsia_hardware_audio::CodecConnector>::Mixin,
                         ddk::Suspendable, ddk::Unbindable>;

class Driver : public Base, public ddk::internal::base_protocol {
 public:
  explicit Driver(zx_device_t* parent, std::shared_ptr<Core> core, bool is_input);

  void Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) override;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn) {
    // Either driver shuts down the whole core.
    core_->Shutdown();
    txn.Reply();
  }
  void DdkSuspend(ddk::SuspendTxn txn) {
    // Either driver shuts down the whole core.
    core_->Shutdown();
    txn.Reply(ZX_OK, txn.requested_state());
  }

 private:
  std::unique_ptr<Server> server_;
  std::shared_ptr<Core> core_;
  bool is_input_;
  Logger logger_;
};

}  // namespace audio::da7219

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_DFV1_H_
