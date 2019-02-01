// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_SM_PACKET_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_SM_PACKET_H_

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/packet_view.h"
#include "garnet/drivers/bluetooth/lib/sm/smp.h"

namespace btlib {
namespace sm {

// Utilities for processing SMP packets.

class PacketReader : public common::PacketView<Header> {
 public:
  explicit PacketReader(const common::ByteBuffer* buffer);
  inline Code code() const { return header().code; }
};

class PacketWriter : public common::MutablePacketView<Header> {
 public:
  // Constructor writes |code| into |buffer|.
  PacketWriter(Code code, common::MutableByteBuffer* buffer);
};

}  // namespace sm
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_SM_PACKET_H_
