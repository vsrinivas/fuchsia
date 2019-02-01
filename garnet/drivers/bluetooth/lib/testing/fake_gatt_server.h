// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_TESTING_FAKE_GATT_SERVER_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_TESTING_FAKE_GATT_SERVER_H_

#include "garnet/drivers/bluetooth/lib/att/att.h"
#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"

namespace btlib {
namespace testing {

class FakeDevice;

// Emulates a GATT server.
class FakeGattServer final {
 public:
  explicit FakeGattServer(FakeDevice* dev);

  // Handle the ATT |pdu| received over link with handle |conn|.
  void HandlePdu(hci::ConnectionHandle conn, const common::ByteBuffer& pdu);

 private:
  void HandleReadByGrpType(hci::ConnectionHandle conn,
                           const common::ByteBuffer& params);

  void Send(hci::ConnectionHandle conn, const common::ByteBuffer& pdu);
  void SendErrorRsp(hci::ConnectionHandle conn, att::OpCode opcode,
                    att::Handle handle, att::ErrorCode ecode);

  // The fake device that owns this server. Must outlive this instance.
  FakeDevice* dev_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeGattServer);
};

}  // namespace testing
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_TESTING_FAKE_GATT_SERVER_H_
