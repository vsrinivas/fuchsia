// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_EVENT_SENDER_H_
#define LIB_FIDL_CPP_EVENT_SENDER_H_

#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/internal/message_sender.h>
#include <lib/zx/channel.h>

#include <utility>

namespace fidl {

// Sends events for |Interface| on a given channel.
//
// An |EventSender| lets its client send events on a given channel. This class
// differs from |Binding| in that |EventSender| does not listen for incoming
// messages on the channel, which allows |EventSender| to send messages from
// multiple threads safely.
//
// See also:
//
//  * |Binding|, which can receive messages as well as send events.
template <typename Interface>
class EventSender final : public fidl::internal::MessageSender {
 public:
  // Constructs an event sender that sends events through |channel|.
  explicit EventSender(zx::channel channel) : channel_(std::move(channel)), stub_(nullptr) {
    stub_.set_sender(this);
  }

  // Constructs an event sender that sends events through the underlying channel
  // in |request|.
  explicit EventSender(InterfaceRequest<Interface> request) : EventSender(request.TakeChannel()) {}

  EventSender(const EventSender&) = delete;
  EventSender& operator=(const EventSender&) = delete;

  // The interface for sending events back to the client.
  typename Interface::EventSender_& events() { return stub_; }

  // The underlying channel.
  const zx::channel& channel() const { return channel_; }

  // Transfers ownership of the underlying channel to the caller.
  zx::channel TakeChannel() { return std::move(channel_); }

 private:
  zx_status_t Send(const fidl_type_t* type, HLCPPOutgoingMessage message) final {
    return fidl::internal::SendMessage(channel_, type, std::move(message));
  }

  zx::channel channel_;
  typename Interface::Stub_ stub_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_EVENT_SENDER_H_
