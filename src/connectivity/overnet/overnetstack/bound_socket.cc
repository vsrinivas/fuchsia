// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/overnetstack/bound_socket.h"
#include <zircon/assert.h>
#include "src/connectivity/overnet/overnetstack/fuchsia_port.h"
#include "src/connectivity/overnet/overnetstack/overnet_app.h"

namespace overnetstack {

namespace {
constexpr size_t kMaxIOSize = 65536;
}

BoundSocket::BoundSocket(OvernetApp* app, overnet::RouterEndpoint::NewStream ns,
                         zx::socket socket)
    : app_(app), overnet_stream_(std::move(ns)), zx_socket_(std::move(socket)) {
  assert(zx_socket_.is_valid());
  // Kick off the two read loops: one from the network and the other from the zx
  // channel. Each proceeds much the same: as data is read, it's written and
  // then the next read is begun.
  StartNetRead();
  StartSocketRead();
}

void BoundSocket::Close(const overnet::Status& status) {
  OVERNET_TRACE(DEBUG) << "CLOSE: " << status << " closed=" << closed_;
  if (closed_) {
    return;
  }
  closed_ = true;
  zx_socket_.reset();
  if (net_recv_) {
    net_recv_->Close(status);
  }
  overnet_stream_.Close(status, [this] { delete this; });
}

void BoundSocket::WriteToSocketAndStartNextRead(std::vector<uint8_t> message,
                                                bool control) {
  assert(pending_write_.empty());
  assert(!pending_share_.is_valid());
  OVERNET_TRACE(DEBUG) << "WriteToSocketAndStartNextRead";
  size_t written;
  auto err = zx_socket_.write(control ? ZX_SOCKET_CONTROL : 0, message.data(),
                              message.size(), &written);
  switch (err) {
    case ZX_OK:
      if (written == message.size()) {
        StartNetRead();
        break;
      } else {
        std::vector<uint8_t>(message.begin() + written, message.end())
            .swap(message);
        [[fallthrough]];
      }
    case ZX_ERR_SHOULD_WAIT:
      // Kernel push back: capture the message, and ask to be informed when we
      // can write.
      pending_write_ = std::move(message);
      err = async_begin_wait(dispatcher_,
                             control ? &wait_ctl_send_.wait : &wait_send_.wait);
      if (err != ZX_OK) {
        Close(overnet::Status::FromZx(err).WithContext(
            "Beginning wait for write"));
      }
      break;
    default:
      // If the write failed, close the stream.
      Close(overnet::Status::FromZx(err).WithContext("Write"));
  }
}

void BoundSocket::ShareToSocketAndStartNextRead(zx::socket socket) {
  assert(!pending_share_.is_valid());
  pending_share_ = std::move(socket);
  if (auto err = async_begin_wait(dispatcher_, &wait_share_.wait);
      err != ZX_OK) {
    Close(overnet::Status::FromZx(err).WithContext("Beginning wait for share"));
  }
}

void BoundSocket::ShareReady(async_dispatcher_t* dispatcher, async_wait_t* wait,
                             zx_status_t status,
                             const zx_packet_signal_t* signal) {
  // Trampoline back into sharing.
  static_assert(offsetof(BoundWait, wait) == 0,
                "The wait must be the first member of BoundWait for this "
                "cast to be valid.");
  reinterpret_cast<BoundWait*>(wait)->stream->OnShareReady(status, signal);
}

void BoundSocket::OnShareReady(zx_status_t status,
                               const zx_packet_signal_t* signal) {
  OVERNET_TRACE(DEBUG) << "OnShareReady: status="
                       << overnet::Status::FromZx(status);
  if (auto err = zx_socket_.share(std::move(pending_share_)); err == ZX_OK) {
    StartNetRead();
  } else {
    Close(overnet::Status::FromZx(err).WithContext("Socket share"));
  }
}

void BoundSocket::StartSocketRead() {
  OVERNET_TRACE(DEBUG) << "StartSocketRead hdl=" << zx_socket_.get();
  if (sock_read_data_) {
    std::vector<uint8_t> message(kMaxIOSize);
    size_t read;
    auto err = zx_socket_.read(0, message.data(), message.size(), &read);
    switch (err) {
      case ZX_OK: {
        message.resize(read);
        proxy_.Message(std::move(message));
        return;
      }
      case ZX_ERR_SHOULD_WAIT:
        sock_read_data_ = false;
        break;
      default:
        // If the read failed, close the stream.
        Close(overnet::Status::FromZx(err).WithContext("ReadData"));
        return;
    }
  }
  if (sock_read_accept_) {
    zx::socket new_socket;
    auto err = zx_socket_.accept(&new_socket);
    switch (err) {
      case ZX_ERR_WRONG_TYPE:
      case ZX_ERR_NOT_SUPPORTED:
        sock_read_accept_ = false;
        break;
      case ZX_OK: {
        zx_info_socket_t socket_info;
        err = new_socket.get_info(ZX_INFO_SOCKET, &socket_info,
                                  sizeof(socket_info), nullptr, nullptr);
        if (err != ZX_OK) {
          Close(
              overnet::Status::FromZx(err).WithContext("Getting socket info"));
          return;
        }
        auto fork_status = overnet_stream_.InitiateFork(
            // TODO(ctiller): unreliable for udp!
            fuchsia::overnet::protocol::ReliabilityAndOrdering::
                ReliableOrdered);
        if (fork_status.is_error()) {
          Close(fork_status.AsStatus());
          return;
        }
        proxy_.Share(fuchsia::overnet::protocol::SocketHandle{
            fork_status->stream_id().as_fidl(), socket_info.options});
        app_->BindSocket(std::move(*fork_status), std::move(new_socket));
        return;
      }
      case ZX_ERR_SHOULD_WAIT:
        sock_read_accept_ = false;
        break;
      default:
        // If the read failed, close the stream.
        Close(overnet::Status::FromZx(err).WithContext("ReadAccept"));
        return;
    }
  }
  if (sock_read_ctl_) {
    std::vector<uint8_t> message(kMaxIOSize);
    size_t read;
    auto err = zx_socket_.read(ZX_SOCKET_CONTROL, message.data(),
                               message.size(), &read);
    switch (err) {
      case ZX_ERR_BAD_STATE:
        sock_read_ctl_ = false;
        break;
      case ZX_OK: {
        message.resize(read);
        proxy_.Control(std::move(message));
        return;
      }
      case ZX_ERR_SHOULD_WAIT:
        sock_read_ctl_ = false;
        break;
      default:
        // If the read failed, close the stream.
        Close(overnet::Status::FromZx(err).WithContext("ReadControl"));
        return;
    }
  }
  assert(!sock_read_ctl_ && !sock_read_data_ && !sock_read_accept_);
  // Kernel push back: ask to be informed when we can try again.
  auto err = async_begin_wait(dispatcher_, &wait_recv_.wait);
  OVERNET_TRACE(DEBUG) << "async_begin_wait result: "
                       << overnet::Status::FromZx(err);
  if (err != ZX_OK) {
    Close(overnet::Status::FromZx(err).WithContext("Beginning wait for read"));
  }
}

void BoundSocket::Proxy::Send_(fidl::Message message) {
  assert(message.handles().size() == 0);
  auto send_slice = overnet::Slice::FromContainer(message.bytes());
  overnet::RouterEndpoint::Stream::SendOp(&socket_->overnet_stream_,
                                          send_slice.length())
      .Push(std::move(send_slice), [this] { socket_->StartSocketRead(); });
}

void BoundSocket::SendReady(async_dispatcher_t* dispatcher, async_wait_t* wait,
                            zx_status_t status,
                            const zx_packet_signal_t* signal) {
  // Trampoline back into writing.
  static_assert(offsetof(BoundWait, wait) == 0,
                "The wait must be the first member of BoundWait for this "
                "cast to be valid.");
  reinterpret_cast<BoundWait*>(wait)->stream->OnSendReady(status, signal);
}

void BoundSocket::OnSendReady(zx_status_t status,
                              const zx_packet_signal_t* signal) {
  OVERNET_TRACE(DEBUG) << "OnSendReady: status="
                       << overnet::Status::FromZx(status);
  WriteToSocketAndStartNextRead(std::move(pending_write_), false);
}

void BoundSocket::CtlSendReady(async_dispatcher_t* dispatcher,
                               async_wait_t* wait, zx_status_t status,
                               const zx_packet_signal_t* signal) {
  // Trampoline back into writing.
  static_assert(offsetof(BoundWait, wait) == 0,
                "The wait must be the first member of BoundWait for this "
                "cast to be valid.");
  reinterpret_cast<BoundWait*>(wait)->stream->OnCtlSendReady(status, signal);
}

void BoundSocket::OnCtlSendReady(zx_status_t status,
                                 const zx_packet_signal_t* signal) {
  OVERNET_TRACE(DEBUG) << "OnCtlSendReady: status="
                       << overnet::Status::FromZx(status);
  WriteToSocketAndStartNextRead(std::move(pending_write_), true);
}

void BoundSocket::RecvReady(async_dispatcher_t* dispatcher, async_wait_t* wait,
                            zx_status_t status,
                            const zx_packet_signal_t* signal) {
  // Trampoline back into reading.
  static_assert(offsetof(BoundWait, wait) == 0,
                "The wait must be the first member of BoundWait for this "
                "cast to be valid.");
  reinterpret_cast<BoundWait*>(wait)->stream->OnRecvReady(status, signal);
}

void BoundSocket::OnRecvReady(zx_status_t status,
                              const zx_packet_signal_t* signal) {
  OVERNET_TRACE(DEBUG) << "OnRecvReady: status="
                       << overnet::Status::FromZx(status)
                       << " observed=" << signal->observed;

  if (status != ZX_OK) {
    Close(overnet::Status::FromZx(status).WithContext("OnRecvReady"));
    return;
  }

  if (signal->observed & ZX_SOCKET_READABLE) {
    sock_read_data_ = true;
  }

  if (signal->observed & ZX_SOCKET_CONTROL_READABLE) {
    sock_read_ctl_ = true;
  }

  if (sock_read_data_ || sock_read_ctl_) {
    StartSocketRead();
    return;
  }

  // Note: we flush all reads before propagating the close.
  ZX_DEBUG_ASSERT(signal->observed & ZX_SOCKET_PEER_CLOSED);
  Close(overnet::Status::Ok());
}

void BoundSocket::StartNetRead() {
  OVERNET_TRACE(DEBUG) << "StartNetRead";
  net_recv_.Reset(&overnet_stream_);
  net_recv_->PullAll(
      [this](overnet::StatusOr<overnet::Optional<std::vector<overnet::Slice>>>
                 status) {
        OVERNET_TRACE(DEBUG) << "StartNetRead got " << status;
        if (status.is_error() || !status->has_value()) {
          // If a read failed, close the stream.
          Close(status.AsStatus());
          return;
        }

        if (closed_) {
          return;
        }

        // Write the message to the channel, then start reading again.
        // Importantly: don't start reading until we've written to ensure
        // that there's push back in the system.
        auto joined =
            overnet::Slice::AlignedJoin((*status)->begin(), (*status)->end());
        auto dispatch_status = overnet::Status::FromZx(stub_.Process_(
            fidl::Message(fidl::BytePart(const_cast<uint8_t*>(joined.begin()),
                                         joined.length(), joined.length()),
                          fidl::HandlePart())));
        if (dispatch_status.is_error()) {
          Close(dispatch_status);
        }
      });
}

void BoundSocket::Stub::Message(std::vector<uint8_t> message) {
  socket_->WriteToSocketAndStartNextRead(std::move(message), false);
}

void BoundSocket::Stub::Control(std::vector<uint8_t> message) {
  socket_->WriteToSocketAndStartNextRead(std::move(message), true);
}

void BoundSocket::Stub::Share(fuchsia::overnet::protocol::SocketHandle hdl) {
  zx_handle_t ha, hb;
  if (auto err = zx_socket_create(hdl.options, &ha, &hb); err != ZX_OK) {
    socket_->Close(
        overnet::Status::FromZx(err).WithContext("Failed to create socket"));
    return;
  }
  zx::socket a(ha);
  zx::socket b(hb);
  auto fork_status = socket_->overnet_stream_.ReceiveFork(
      hdl.stream_id,
      // TODO(ctiller): unreliable for udp!
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered);
  if (fork_status.is_error()) {
    socket_->Close(fork_status.AsStatus());
    return;
  }
  socket_->app_->BindSocket(std::move(*fork_status), std::move(b));
  socket_->ShareToSocketAndStartNextRead(std::move(a));
}

}  // namespace overnetstack
