// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/data/socket_factory.cc"
#include "garnet/drivers/bluetooth/lib/rfcomm/channel.h"

namespace btlib::data::internal {
template class SocketFactory<rfcomm::Channel>;
}  // namespace btlib::data::internal
