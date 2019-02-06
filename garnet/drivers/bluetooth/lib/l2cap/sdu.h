// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_SDU_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_SDU_H_

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"

namespace btlib {
namespace l2cap {

// TODO(armansito): For now we only support basic mode in which 1 SDU = 1 PDU.
// Revisit once we support other modes.
using SDU = common::ByteBufferPtr;

}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_SDU_H_
