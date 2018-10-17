// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_DATA_RFCOMM_SOCKET_CHANNEL_RELAY_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_DATA_RFCOMM_SOCKET_CHANNEL_RELAY_H_

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/data/socket_channel_relay.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/channel.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/rfcomm.h"

namespace btlib::data::internal {
using RfcommSocketChannelRelay =
    SocketChannelRelay<rfcomm::Channel, rfcomm::Channel::UniqueId,
                       common::ByteBufferPtr>;
}  // namespace btlib::data::internal

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_DATA_RFCOMM_SOCKET_CHANNEL_RELAY_H_
