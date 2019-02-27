// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/overnet/protocol/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/wait.h>
#include <lib/fidl/cpp/message.h>
#include <lib/zx/socket.h>
#include "garnet/lib/overnet/endpoint/router_endpoint.h"

namespace overnetstack {

class OvernetApp;

// Creates a stream by combining a zx::socket with an overnet DatagramStream.
// Reads from the overnet stream become writes to the zx socket, and vice
// versa. Errors are propagated.
// TODO(ctiller): epitaph support.
// TODO(ctiller): rewrite messages to:
// - support some limited handle propagation across overnet.
// - ensure system messages are never propagated.
class BoundSocket {
 public:
  BoundSocket(OvernetApp* app, overnet::RouterEndpoint::NewStream ns,
              zx::socket socket);

 private:
  void Close(const overnet::Status& status);
  void StartNetRead();

  void WriteToSocketAndStartNextRead(std::vector<uint8_t> message,
                                     bool control);
  void ShareToSocketAndStartNextRead(zx::socket socket);

  void StartSocketRead();

  struct BoundWait {
    async_wait_t wait;
    BoundSocket* stream;
  };

  static void SendReady(async_dispatcher_t* dispatcher, async_wait_t* wait,
                        zx_status_t status, const zx_packet_signal_t* signal);
  void OnSendReady(zx_status_t status, const zx_packet_signal_t* signal);
  static void CtlSendReady(async_dispatcher_t* dispatcher, async_wait_t* wait,
                           zx_status_t status,
                           const zx_packet_signal_t* signal);
  void OnCtlSendReady(zx_status_t status, const zx_packet_signal_t* signal);
  static void RecvReady(async_dispatcher_t* dispatcher, async_wait_t* wait,
                        zx_status_t status, const zx_packet_signal_t* signal);
  void OnRecvReady(zx_status_t status, const zx_packet_signal_t* signal);
  static void ShareReady(async_dispatcher_t* dispatcher, async_wait_t* wait,
                         zx_status_t status, const zx_packet_signal_t* signal);
  void OnShareReady(zx_status_t status, const zx_packet_signal_t* signal);

  class Proxy final : public fuchsia::overnet::protocol::ZirconSocket_Proxy {
   public:
    Proxy(BoundSocket* socket) : socket_(socket) {}

    void Send_(fidl::Message message) override;

   private:
    BoundSocket* const socket_;
  };

  class Stub final : public fuchsia::overnet::protocol::ZirconSocket_Stub {
   public:
    Stub(BoundSocket* socket) : socket_(socket) {}

    void Send_(fidl::Message message) override { abort(); }

    void Message(std::vector<uint8_t> message) override;
    void Control(std::vector<uint8_t> message) override;
    void Share(fuchsia::overnet::protocol::SocketHandle socket) override;

   private:
    BoundSocket* const socket_;
  };

  OvernetApp* const app_;
  Proxy proxy_{this};
  Stub stub_{this};
  async_dispatcher_t* const dispatcher_ = async_get_default_dispatcher();
  bool closed_ = false;
  overnet::RouterEndpoint::Stream overnet_stream_;
  zx::socket zx_socket_;
  overnet::Optional<overnet::RouterEndpoint::Stream::ReceiveOp> net_recv_;
  std::vector<uint8_t> pending_write_;
  zx::socket pending_share_;
  bool sock_read_data_ = true;
  bool sock_read_ctl_ = true;
  bool sock_read_accept_ = true;
  BoundWait wait_send_{{{ASYNC_STATE_INIT},
                        &BoundSocket::SendReady,
                        zx_socket_.get(),
                        ZX_SOCKET_WRITABLE},
                       this};
  BoundWait wait_ctl_send_{{{ASYNC_STATE_INIT},
                            &BoundSocket::CtlSendReady,
                            zx_socket_.get(),
                            ZX_SOCKET_CONTROL_WRITABLE},
                           this};
  BoundWait wait_share_{{{ASYNC_STATE_INIT},
                         &BoundSocket::ShareReady,
                         zx_socket_.get(),
                         ZX_SOCKET_SHARE},
                        this};
  BoundWait wait_recv_{{{ASYNC_STATE_INIT},
                        &BoundSocket::RecvReady,
                        zx_socket_.get(),
                        ZX_SOCKET_READABLE | ZX_SOCKET_CONTROL_READABLE |
                            ZX_SOCKET_ACCEPT | ZX_SOCKET_PEER_CLOSED},
                       this};
};

}  // namespace overnetstack
