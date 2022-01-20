// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_CHANNEL_H_
#define LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_CHANNEL_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/async/dispatcher.h>
#include <lib/async/wait.h>
#include <lib/fidl/llcpp/internal/endpoints.h>
#include <lib/fidl/llcpp/internal/transport.h>
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>

namespace fidl {

template <typename Protocol>
class ClientEnd;
template <typename Protocol>
class UnownedClientEnd;
template <typename Protocol>
class ServerEnd;
template <typename Protocol>
class ServerBindingRef;
template <typename FidlMethod>
class WireUnownedResult;

namespace internal {

struct ChannelTransport {
  using OwnedType = zx::channel;
  using UnownedType = zx::unowned_channel;
  template <typename Protocol>
  using ClientEnd = fidl::ClientEnd<Protocol>;
  template <typename Protocol>
  using UnownedClientEnd = fidl::UnownedClientEnd<Protocol>;
  template <typename Protocol>
  using ServerEnd = fidl::ServerEnd<Protocol>;
  template <typename Protocol>
  using ServerBindingRef = fidl::ServerBindingRef<Protocol>;
  template <typename FidlMethod>
  using WireUnownedResult = fidl::WireUnownedResult<FidlMethod>;
  using HandleMetadata = fidl_channel_handle_metadata_t;
  using IncomingTransportContextType = struct {};
  using OutgoingTransportContextType = struct {};

  static constexpr bool TransportProvidesReadBuffer = false;

  static const TransportVTable VTable;
  static const CodingConfig EncodingConfiguration;
};

template <>
struct AssociatedTransportImpl<zx::channel> {
  using type = ChannelTransport;
};
template <>
struct AssociatedTransportImpl<zx::unowned_channel> {
  using type = ChannelTransport;
};

template <>
struct AssociatedTransportImpl<fidl_channel_handle_metadata_t> {
  using type = ChannelTransport;
};

static_assert(sizeof(fidl_handle_t) == sizeof(zx_handle_t));

class ChannelWaiter : private async_wait_t, public TransportWaiter {
 public:
  ChannelWaiter(fidl_handle_t handle, async_dispatcher_t* dispatcher,
                TransportWaitSuccessHandler success_handler,
                TransportWaitFailureHandler failure_handler)
      : async_wait_t({{ASYNC_STATE_INIT},
                      &ChannelWaiter::OnWaitFinished,
                      handle,
                      ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE,
                      0}),
        dispatcher_(dispatcher),
        success_handler_(std::move(success_handler)),
        failure_handler_(std::move(failure_handler)) {}
  zx_status_t Begin() override {
    return async_begin_wait(dispatcher_, static_cast<async_wait_t*>(this));
  }
  zx_status_t Cancel() override {
    return async_cancel_wait(dispatcher_, static_cast<async_wait_t*>(this));
  }

 private:
  static void OnWaitFinished(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                             const zx_packet_signal_t* signal) {
    static_cast<ChannelWaiter*>(wait)->HandleWaitFinished(dispatcher, status, signal);
  }

  void HandleWaitFinished(async_dispatcher_t* dispatcher, zx_status_t status,
                          const zx_packet_signal_t* signal);

  async_dispatcher_t* dispatcher_;
  TransportWaitSuccessHandler success_handler_;
  TransportWaitFailureHandler failure_handler_;
};

}  // namespace internal

// The client endpoint of a FIDL channel.
//
// The remote (server) counterpart of the channel expects this end of the
// channel to speak the protocol represented by |Protocol|. This type is the
// dual of |ServerEnd|.
//
// |ClientEnd| is thread-compatible: it may be transferred to another thread
// or another process.
template <typename Protocol>
class ClientEnd final : public internal::ClientEndBase<Protocol, internal::ChannelTransport> {
  static_assert(std::is_same_v<typename Protocol::Transport, internal::ChannelTransport>);
  using ClientEndBase = internal::ClientEndBase<Protocol, internal::ChannelTransport>;

 public:
  using ClientEndBase::ClientEndBase;

  // The underlying channel.
  const zx::channel& channel() const { return ClientEndBase::handle_; }
  zx::channel& channel() { return ClientEndBase::handle_; }

  // Transfers ownership of the underlying channel to the caller.
  zx::channel TakeChannel() { return std::move(ClientEndBase::handle_); }
};

// A typed client endpoint that does not claim ownership. It is typically
// created from an owning |fidl::ClientEnd<Protocol>|.
// These types are used by generated FIDL APIs that do not take ownership.
//
// The remote (server) counterpart of the channel expects this end of the
// channel to speak the protocol represented by |Protocol|.
//
// Compared to a |const fidl::ClientEnd<Protocol>&|,
// |fidl::UnownedClientEnd<Protocol>| has the additional flexibility of being
// able to be stored in a member variable or field, while still remembering
// the associated FIDL protocol.
template <typename Protocol>
class UnownedClientEnd final
    : public internal::UnownedClientEndBase<Protocol, internal::ChannelTransport> {
  static_assert(std::is_same_v<typename Protocol::Transport, internal::ChannelTransport>);
  using UnownedClientEndBase = internal::UnownedClientEndBase<Protocol, internal::ChannelTransport>;

 public:
  using UnownedClientEndBase::UnownedClientEndBase;

  zx::unowned_channel channel() const { return zx::unowned_channel(UnownedClientEndBase::handle_); }
};

// The server endpoint of a FIDL handle.
//
// The remote (client) counterpart of the handle expects this end of the
// handle to serve the protocol represented by |Protocol|. This type is the
// dual of |ClientEnd|.
//
// |ServerEnd| is thread-compatible: the caller should not use the underlying
// handle (e.g. sending an event) while the server-end object is being mutated
// in a different thread.
template <typename Protocol>
class ServerEnd : public internal::ServerEndBase<Protocol, internal::ChannelTransport> {
  static_assert(cpp17::is_same_v<typename Protocol::Transport, internal::ChannelTransport>);
  using ServerEndBase = internal::ServerEndBase<Protocol, internal::ChannelTransport>;

 public:
  using ServerEndBase::ServerEndBase;

  const zx::channel& channel() const { return ServerEndBase::handle_; }
  zx::channel& channel() { return ServerEndBase::handle_; }

  // Transfers ownership of the underlying channel to the caller.
  zx::channel TakeChannel() { return ServerEndBase::TakeHandle(); }

  // Sends an epitaph over the underlying channel, then closes the channel.
  // An epitaph is a final optional message sent over a server-end towards
  // the client, before the server-end is closed down. See the FIDL
  // language spec for more information about epitaphs.
  //
  // The server-end must be holding a valid underlying channel.
  // Returns the status of the channel write operation.
  zx_status_t Close(zx_status_t epitaph_value) {
    if (!ServerEndBase::is_valid()) {
      ZX_PANIC("Cannot close an invalid ServerEnd.");
    }
    zx::channel channel = TakeChannel();
    return fidl_epitaph_write(channel.get(), epitaph_value);
  }
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_CHANNEL_H_
