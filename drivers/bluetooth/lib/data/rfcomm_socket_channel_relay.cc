// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/rfcomm/channel.h"

// These functions are |inline|, |static|, and in an unnamed-namespace, to avoid
// violating the one-definition rule. See
// https://en.cppreference.com/w/cpp/language/definition and
// https://en.cppreference.com/w/cpp/language/inline
//
// It may be redundant to place the definitions in an unnamed namespace,
// and declare them static, since either option should give the functions
// internal linkage. However, the |inline| documentation cited above only
// explicitly mentions |static| as an example of non-external linkage.
namespace {
using BufT = btlib::rfcomm::Channel::PacketType;
static inline bool ValidateRxData(const BufT& buf) { return buf != nullptr; }
static inline size_t GetRxDataLen(const BufT& buf) { return buf->size(); }
static inline bool InvokeWithRxData(
    fit::function<void(btlib::common::ByteBuffer& data)> callback,
    const BufT& buf) {
  callback(*buf);
  return true;
}
}  // namespace

#include "garnet/drivers/bluetooth/lib/data/socket_channel_relay.cc"

namespace btlib::data::internal {
template class SocketChannelRelay<rfcomm::Channel>;
}  // namespace btlib::data::internal
