// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/internal/client_details.h>

namespace fidl::internal {

fidl::Status SyncEventHandler::HandleOneEventImpl_(zx::unowned_channel channel,
                                                   ChannelMessageStorageView storage,
                                                   IncomingEventDispatcherBase& dispatcher) {
  zx_status_t status = channel->wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                         ::zx::time::infinite(), nullptr);
  if (status != ZX_OK) {
    return fidl::Status::TransportError(status, kErrorWaitOneFailed);
  }
  fidl::IncomingHeaderAndMessage msg =
      fidl::MessageRead(channel, storage, ReadOptions{.discardable = true});
  if (msg.status() == ZX_ERR_BUFFER_TOO_SMALL) {
    // Message size is unexpectedly larger than calculated.
    // This can only be due to a newer version of the protocol defining a new event,
    // whose size exceeds the maximum of known events in the current protocol.
    return fidl::Status::UnexpectedMessage(ZX_ERR_BUFFER_TOO_SMALL, kErrorSyncEventBufferTooSmall);
  }
  if (!msg.ok()) {
    return msg;
  }
  got_transitional_ = false;
  fidl::Status dispatch_status = dispatcher.DispatchEvent(msg, &storage);
  if (got_transitional_) {
    return fidl::Status::UnexpectedMessage(ZX_ERR_NOT_SUPPORTED,
                                           kErrorSyncEventUnhandledTransitionalEvent);
  }
  return dispatch_status;
}

void SyncEventHandler::OnTransitionalEvent_() {
  ZX_DEBUG_ASSERT(!got_transitional_);
  got_transitional_ = true;
}

fidl::Status IncomingEventDispatcherBase::DispatchEvent(
    fidl::IncomingHeaderAndMessage& msg, internal::MessageStorageViewBase* storage_view) {
  return fidl::Status::UnknownOrdinal();
}

}  // namespace fidl::internal
