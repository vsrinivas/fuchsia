// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/overnet/protocol/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/wait.h>
#include <lib/fidl/cpp/message.h>
#include <lib/zx/channel.h>
#include "src/connectivity/overnet/deprecated/lib/endpoint/router_endpoint.h"

namespace overnetstack {

class OvernetApp;

// Creates a stream by combining a zx::channel with an overnet DatagramStream.
// Reads from the overnet stream become writes to the zx channel, and vice
// versa. Errors are propagated.
// TODO(ctiller): epitaph support.
// TODO(ctiller): rewrite messages to:
// - support some limited handle propagation across overnet.
// - ensure system messages are never propagated.
class BoundChannel {
 public:
  BoundChannel(OvernetApp* app, overnet::RouterEndpoint::NewStream ns, zx::channel channel);

 private:
  ~BoundChannel() = default;
  void Close(const overnet::Status& status);
  void StartNetRead();
  void Ref() { ++refs_; }
  void Unref() {
    if (0 == --refs_) {
      delete this;
    }
  }

  overnet::StatusOr<fuchsia::overnet::protocol::ZirconChannelMessage> EncodeMessage(
      fidl::Message message);
  // Calls `then` with a fidl::Message; the decoded fidl::Message may point into
  // message.
  overnet::Status DecodeMessageThen(fuchsia::overnet::protocol::ZirconChannelMessage* message,
                                    fit::function<overnet::Status(fidl::Message)> then);
  void WriteToChannelAndStartNextRead(fidl::Message message);

  void StartChannelRead();

  struct BoundWait {
    async_wait_t wait;
    BoundChannel* stream;
  };

  static void SendReady(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);
  void OnSendReady(zx_status_t status, const zx_packet_signal_t* signal);
  static void RecvReady(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);
  void OnRecvReady(zx_status_t status, const zx_packet_signal_t* signal);

  class Proxy final : public fuchsia::overnet::protocol::ZirconChannel_Proxy {
   public:
    Proxy(BoundChannel* channel) : channel_(channel) {}

    void Send_(fidl::Message message) override;

   private:
    BoundChannel* const channel_;
  };

  class Stub final : public fuchsia::overnet::protocol::ZirconChannel_Stub {
   public:
    Stub(BoundChannel* channel) : channel_(channel) {}

    void Send_(fidl::Message message) override { abort(); }

    void Message(fuchsia::overnet::protocol::ZirconChannelMessage message) override;

   private:
    BoundChannel* const channel_;
  };

  OvernetApp* const app_;
  Proxy proxy_{this};
  Stub stub_{this};
  async_dispatcher_t* const dispatcher_ = async_get_default_dispatcher();
  bool closed_ = false;
  int refs_ = 1;
  overnet::RouterEndpoint::Stream overnet_stream_;
  zx::channel zx_channel_;
  overnet::Optional<overnet::RouterEndpoint::Stream::ReceiveOp> net_recv_;
  std::vector<uint8_t> pending_chan_bytes_;
  std::vector<zx::handle> pending_chan_handles_;
  BoundWait wait_send_{
      {{ASYNC_STATE_INIT}, &BoundChannel::SendReady, zx_channel_.get(), ZX_CHANNEL_WRITABLE, 0},
      this};
  BoundWait wait_recv_{{{ASYNC_STATE_INIT},
                        &BoundChannel::RecvReady,
                        zx_channel_.get(),
                        ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                        0},
                       this};
};

}  // namespace overnetstack
