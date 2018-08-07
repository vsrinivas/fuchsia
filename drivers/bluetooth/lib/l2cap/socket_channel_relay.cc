// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "socket_channel_relay.h"

#include <utility>

#include <lib/async/default.h>

#include "lib/fxl/logging.h"

namespace btlib {
namespace l2cap {

namespace internal {

SocketChannelRelay::SocketChannelRelay(zx::socket&& socket,
                                       fbl::RefPtr<Channel> channel,
                                       DeactivationCallback deactivation_cb)
    : state_(RelayState::kActivating),
      socket_(std::move(socket)),
      channel_(channel),
      dispatcher_(async_get_default_dispatcher()),
      deactivation_cb_(std::move(deactivation_cb)) {
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(socket_);
  FXL_DCHECK(channel_);
}

SocketChannelRelay::~SocketChannelRelay() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  if (state_ != RelayState::kDeactivated) {
    FXL_VLOG(5) << "l2cap: Deactivating relay for channel " << channel_->id()
                << " in dtor; will require Channel's mutex";
    Deactivate();
  }
}

void SocketChannelRelay::Deactivate() { FXL_NOTIMPLEMENTED(); }

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
