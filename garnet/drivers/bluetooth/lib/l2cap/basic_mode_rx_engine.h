// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_BASIC_MODE_RX_ENGINE_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_BASIC_MODE_RX_ENGINE_H_

#include "garnet/drivers/bluetooth/lib/l2cap/rx_engine.h"

namespace btlib {
namespace l2cap {
namespace internal {

// Implements the receiver-side functionality of L2CAP Basic Mode. See Bluetooth
// Core Spec v5.0, Volume 3, Part A, Sec 2.4, "Modes of Operation".
class BasicModeRxEngine final : public RxEngine {
 public:
  BasicModeRxEngine() = default;
  virtual ~BasicModeRxEngine() = default;

  common::ByteBufferPtr ProcessPdu(PDU) override;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(BasicModeRxEngine);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_BASIC_MODE_RX_ENGINE_H_
