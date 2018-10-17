// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "l2cap_socket_channel_relay.h"

#include "garnet/drivers/bluetooth/lib/data/socket_channel_relay.cc"

namespace btlib::data::internal {
template class SocketChannelRelay<l2cap::Channel, l2cap::Channel::UniqueId,
                                  l2cap::SDU>;
}  // namespace btlib::data::internal
