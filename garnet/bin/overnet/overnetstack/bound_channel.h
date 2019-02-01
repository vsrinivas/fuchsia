// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/wait.h>
#include <lib/fidl/cpp/message.h>
#include <lib/zx/channel.h>
#include "garnet/lib/overnet/endpoint/router_endpoint.h"

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
  BoundChannel(OvernetApp* app, overnet::RouterEndpoint::NewStream ns,
               zx::channel channel);

 private:
  class FidlMessageBuilder;

  void Close(const overnet::Status& status);
  void StartNetRead();
  void WriteToChannelAndStartNextRead(
      std::unique_ptr<FidlMessageBuilder> builder);

  overnet::StatusOr<overnet::Slice> ChannelMessageToOvernet(
      fidl::Message message);

  void StartChannelRead();

  struct BoundWait {
    async_wait_t wait;
    BoundChannel* stream;
  };

  static void SendReady(async_dispatcher_t* dispatcher, async_wait_t* wait,
                        zx_status_t status, const zx_packet_signal_t* signal);
  void OnSendReady(zx_status_t status, const zx_packet_signal_t* signal);
  static void RecvReady(async_dispatcher_t* dispatcher, async_wait_t* wait,
                        zx_status_t status, const zx_packet_signal_t* signal);
  void OnRecvReady(zx_status_t status, const zx_packet_signal_t* signal);

  OvernetApp* const app_;
  async_dispatcher_t* const dispatcher_ = async_get_default_dispatcher();
  bool closed_ = false;
  overnet::RouterEndpoint::Stream overnet_stream_;
  zx::channel zx_channel_;
  overnet::Optional<overnet::RouterEndpoint::Stream::ReceiveOp> net_recv_;
  std::unique_ptr<FidlMessageBuilder> waiting_to_write_;
  BoundWait wait_send_{{{ASYNC_STATE_INIT},
                        &BoundChannel::SendReady,
                        zx_channel_.get(),
                        ZX_CHANNEL_WRITABLE},
                       this};
  BoundWait wait_recv_{{{ASYNC_STATE_INIT},
                        &BoundChannel::RecvReady,
                        zx_channel_.get(),
                        ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED},
                       this};
};

}  // namespace overnetstack
