// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_DATA_L2CAP_SOCKET_CHANNEL_RELAY_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_DATA_L2CAP_SOCKET_CHANNEL_RELAY_H_

#include "garnet/drivers/bluetooth/lib/data/socket_channel_relay.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap.h"
#include "garnet/drivers/bluetooth/lib/l2cap/sdu.h"

namespace btlib::data::internal {
using L2capSocketChannelRelay = SocketChannelRelay<l2cap::Channel, l2cap::SDU>;
}  // namespace btlib::data::internal

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_DATA_L2CAP_SOCKET_CHANNEL_RELAY_H_
