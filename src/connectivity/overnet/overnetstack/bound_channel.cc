// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/overnetstack/bound_channel.h"
#include <lib/zx/socket.h>
#include <zircon/assert.h>
#include "src/connectivity/overnet/lib/protocol/coding.h"
#include "src/connectivity/overnet/overnetstack/fuchsia_port.h"
#include "src/connectivity/overnet/overnetstack/overnet_app.h"

namespace overnetstack {

BoundChannel::BoundChannel(OvernetApp* app,
                           overnet::RouterEndpoint::NewStream ns,
                           zx::channel channel)
    : app_(app),
      overnet_stream_(std::move(ns)),
      zx_channel_(std::move(channel)) {
  assert(zx_channel_.is_valid());

  // Kick off the two read loops: one from the network and the other from the zx
  // channel. Each proceeds much the same: as data is read, it's written and
  // then the next read is begun.
  StartNetRead();
  StartChannelRead();
}

void BoundChannel::Close(const overnet::Status& status) {
  OVERNET_TRACE(TRACE) << "CLOSE: " << status << " closed=" << closed_;
  if (closed_) {
    return;
  }
  closed_ = true;
  zx_channel_.reset();
  if (net_recv_) {
    net_recv_->Close(status);
  }
  overnet_stream_.Close(status, [this] { Unref(); });
}

void BoundChannel::WriteToChannelAndStartNextRead(fidl::Message message) {
  OVERNET_TRACE(TRACE) << "WriteToChannelAndStartNextRead hdl="
                       << zx_channel_.get();
  auto err = message.Write(zx_channel_.get(), 0);
  switch (err) {
    case ZX_OK:
      pending_chan_bytes_.clear();
      pending_chan_handles_.clear();
      StartNetRead();
      break;
    case ZX_ERR_SHOULD_WAIT:
      // Kernel push back: capture the slice, and ask to be informed when we
      // can write.
      std::vector<uint8_t>(message.bytes().begin(), message.bytes().end())
          .swap(pending_chan_bytes_);
      std::vector<zx::handle>(message.handles().begin(),
                              message.handles().end())
          .swap(pending_chan_handles_);
      message.ClearHandlesUnsafe();
      Ref();
      err = async_begin_wait(dispatcher_, &wait_send_.wait);
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

void BoundChannel::StartChannelRead() {
  OVERNET_TRACE(TRACE) << "StartChannelRead hdl=" << zx_channel_.get();
  struct Buffer {
    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  };
  auto buffer = std::make_unique<Buffer>();
  fidl::Message message(
      fidl::BytePart(buffer->bytes, ZX_CHANNEL_MAX_MSG_BYTES),
      fidl::HandlePart(buffer->handles, ZX_CHANNEL_MAX_MSG_HANDLES));
  auto err = message.Read(zx_channel_.get(), 0);
  OVERNET_TRACE(DEBUG) << "StartChannelRead read result: "
                       << overnet::Status::FromZx(err);
  switch (err) {
    case ZX_OK: {
      auto encoded = EncodeMessage(std::move(message));
      if (encoded.is_error()) {
        Close(encoded.AsStatus());
        return;
      }
      proxy_.Message(std::move(*encoded));
    } break;
    case ZX_ERR_SHOULD_WAIT:
      // Kernel push back: ask to be informed when we can try again.
      Ref();
      err = async_begin_wait(dispatcher_, &wait_recv_.wait);
      OVERNET_TRACE(DEBUG) << "async_begin_wait result: "
                           << overnet::Status::FromZx(err);
      if (err != ZX_OK) {
        Close(overnet::Status::FromZx(err).WithContext(
            "Beginning wait for read"));
        Unref();
        break;
      }
      break;
    default:
      // If the read failed, close the stream.
      Close(overnet::Status::FromZx(err).WithContext("Read"));
      break;
  }
}

void BoundChannel::Proxy::Send_(fidl::Message message) {
  assert(message.handles().size() == 0);
  auto send_slice =
      *overnet::Encode(overnet::Slice::FromContainer(message.bytes()));
  channel_->Ref();
  overnet::RouterEndpoint::Stream::SendOp(&channel_->overnet_stream_,
                                          send_slice.length())
      .Push(std::move(send_slice), [this] {
        channel_->StartChannelRead();
        channel_->Unref();
      });
}

void BoundChannel::SendReady(async_dispatcher_t* dispatcher, async_wait_t* wait,
                             zx_status_t status,
                             const zx_packet_signal_t* signal) {
  // Trampoline back into writing.
  static_assert(offsetof(BoundWait, wait) == 0,
                "The wait must be the first member of BoundWait for this "
                "cast to be valid.");
  reinterpret_cast<BoundWait*>(wait)->stream->OnSendReady(status, signal);
}

void BoundChannel::OnSendReady(zx_status_t status,
                               const zx_packet_signal_t* signal) {
  OVERNET_TRACE(TRACE) << "OnSendReady: status="
                       << overnet::Status::FromZx(status);
  std::vector<zx_handle_t> handles;
  for (auto& h : pending_chan_handles_) {
    handles.push_back(h.release());
  }
  WriteToChannelAndStartNextRead(fidl::Message(
      fidl::BytePart(pending_chan_bytes_.data(), pending_chan_bytes_.size(),
                     pending_chan_bytes_.size()),
      fidl::HandlePart(handles.data(), handles.size(), handles.size())));
  Unref();
}

void BoundChannel::RecvReady(async_dispatcher_t* dispatcher, async_wait_t* wait,
                             zx_status_t status,
                             const zx_packet_signal_t* signal) {
  // Trampoline back into reading.
  static_assert(offsetof(BoundWait, wait) == 0,
                "The wait must be the first member of BoundWait for this "
                "cast to be valid.");
  reinterpret_cast<BoundWait*>(wait)->stream->OnRecvReady(status, signal);
}

void BoundChannel::OnRecvReady(zx_status_t status,
                               const zx_packet_signal_t* signal) {
  OVERNET_TRACE(TRACE) << "OnRecvReady: status="
                       << overnet::Status::FromZx(status)
                       << " observed=" << signal->observed;

  if (status != ZX_OK) {
    Close(overnet::Status::FromZx(status).WithContext("OnRecvReady"));
    Unref();
    return;
  }

  if (signal->observed & ZX_CHANNEL_READABLE) {
    StartChannelRead();
    Unref();
    return;
  }

  // Note: we flush all reads before propagating the close.
  ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
  Close(overnet::Status::Ok());
  Unref();
}

void BoundChannel::StartNetRead() {
  OVERNET_TRACE(DEBUG) << "StartNetRead";
  net_recv_.Reset(&overnet_stream_);
  Ref();
  net_recv_->PullAll(
      [this](overnet::StatusOr<overnet::Optional<std::vector<overnet::Slice>>>
                 status) {
        OVERNET_TRACE(DEBUG) << "StartNetRead got " << status;
        if (status.is_error() || !status->has_value()) {
          // If a read failed, close the stream.
          Close(status.AsStatus());
          Unref();
          return;
        }

        if (closed_) {
          Unref();
          return;
        }

        // Write the message to the channel, then start reading again.
        // Importantly: don't start reading until we've written to ensure
        // that there's push back in the system.
        auto decode_status = overnet::Decode(
            overnet::Slice::Join((*status)->begin(), (*status)->end()));
        if (decode_status.is_error()) {
          // Failed to decode: close stream
          Close(decode_status.AsStatus());
          Unref();
          return;
        }

        auto packet = overnet::Slice::Aligned(std::move(*decode_status));

        if (auto process_status = stub_.Process_(fidl::Message(
                fidl::BytePart(const_cast<uint8_t*>(packet.begin()),
                               packet.length(), packet.length()),
                fidl::HandlePart()));
            process_status != ZX_OK) {
          Close(overnet::Status::FromZx(process_status)
                    .WithContext("Processing incoming channel message"));
          Unref();
          return;
        }

        Unref();
      });
}

void BoundChannel::Stub::Message(
    fuchsia::overnet::protocol::ZirconChannelMessage message) {
  if (auto status = channel_->DecodeMessageThen(
          &message,
          [this](fidl::Message fidl_msg) {
            channel_->WriteToChannelAndStartNextRead(std::move(fidl_msg));
            return overnet::Status::Ok();
          });
      status.is_error()) {
    channel_->Close(status);
  }
}

overnet::StatusOr<fuchsia::overnet::protocol::ZirconChannelMessage>
BoundChannel::EncodeMessage(fidl::Message message) {
  if (!message.has_header()) {
    return overnet::Status(overnet::StatusCode::FAILED_PRECONDITION,
                           "FIDL message without a header");
  }
  assert(message.flags() == 0);
  fuchsia::overnet::protocol::ZirconChannelMessage msg;
  std::vector<uint8_t>(message.bytes().begin(), message.bytes().end())
      .swap(msg.bytes);

  // Keep track of failure.
  // We cannot leave the loop below early or we risk leaking handles.
  overnet::Status status = overnet::Status::Ok();
  for (auto handle : message.handles()) {
    zx::handle hdl(handle);
    if (status.is_error()) {
      continue;
    }
    zx_info_handle_basic_t info;
    auto err = hdl.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr,
                            nullptr);
    if (err != ZX_OK) {
      status = overnet::Status::FromZx(err).WithContext("Getting handle type");
      continue;
    }
    fuchsia::overnet::protocol::ZirconHandle zh;
    switch (info.type) {
      case ZX_OBJ_TYPE_CHANNEL: {
        auto fork_status = overnet_stream_.InitiateFork(
            fuchsia::overnet::protocol::ReliabilityAndOrdering::
                ReliableOrdered);
        if (fork_status.is_error()) {
          status = fork_status.AsStatus();
          continue;
        }
        zh.set_channel(fuchsia::overnet::protocol::ChannelHandle{
            fork_status->stream_id().as_fidl()});
        auto channel = zx::channel(hdl.release());
        assert(channel.is_valid());
        assert(!hdl.is_valid());
        app_->BindChannel(std::move(*fork_status), std::move(channel));
        assert(!channel.is_valid());
      } break;
      case ZX_OBJ_TYPE_SOCKET: {
        zx_info_socket_t socket_info;
        err = hdl.get_info(ZX_INFO_SOCKET, &socket_info, sizeof(socket_info),
                           nullptr, nullptr);
        if (err != ZX_OK) {
          status =
              overnet::Status::FromZx(err).WithContext("Getting socket info");
          continue;
        }
        auto fork_status = overnet_stream_.InitiateFork(
            // TODO(ctiller): unreliable for udp!
            fuchsia::overnet::protocol::ReliabilityAndOrdering::
                ReliableOrdered);
        if (fork_status.is_error()) {
          status = fork_status.AsStatus();
          continue;
        }
        zh.set_socket(fuchsia::overnet::protocol::SocketHandle{
            fork_status->stream_id().as_fidl(), socket_info.options});
        auto socket = zx::socket(hdl.release());
        assert(socket.is_valid());
        assert(!hdl.is_valid());
        app_->BindSocket(std::move(*fork_status), std::move(socket));
        assert(!socket.is_valid());
      } break;
      default:
        status = overnet::Status(overnet::StatusCode::INVALID_ARGUMENT,
                                 "Bad handle type");
        continue;
    }
    assert(status.is_ok());
    msg.handles.emplace_back(std::move(zh));
  }
  message.ClearHandlesUnsafe();
  if (status.is_error()) {
    return status;
  }
  return msg;
}

overnet::Status BoundChannel::DecodeMessageThen(
    fuchsia::overnet::protocol::ZirconChannelMessage* message,
    fit::function<overnet::Status(fidl::Message)> then) {
  std::vector<zx::handle> handle_objects;

  for (const auto& h : message->handles) {
    switch (h.Which()) {
      case fuchsia::overnet::protocol::ZirconHandle::Tag::kChannel: {
        zx::channel a, b;
        if (auto err = zx::channel::create(0, &a, &b); err != ZX_OK) {
          return overnet::Status::FromZx(err).WithContext("zx_channel_create");
        }
        auto fork_status = overnet_stream_.ReceiveFork(
            h.channel().stream_id, fuchsia::overnet::protocol::
                                       ReliabilityAndOrdering::ReliableOrdered);
        if (fork_status.is_error()) {
          return fork_status.AsStatus();
        }
        app_->BindChannel(std::move(*fork_status), std::move(a));
        handle_objects.push_back(std::move(b));
      } break;
      case fuchsia::overnet::protocol::ZirconHandle::Tag::kSocket: {
        zx::socket a, b;
        if (auto err = zx::socket::create(h.socket().options, &a, &b);
            err != ZX_OK) {
          return overnet::Status::FromZx(err).WithContext("zx_socket_create");
        }
        auto fork_status = overnet_stream_.ReceiveFork(
            h.channel().stream_id,
            // TODO(ctiller): unreliable for udp
            fuchsia::overnet::protocol::ReliabilityAndOrdering::
                ReliableOrdered);
        if (fork_status.is_error()) {
          return fork_status.AsStatus();
        }
        app_->BindSocket(std::move(*fork_status), std::move(a));
        handle_objects.push_back(std::move(b));
      } break;
      case fuchsia::overnet::protocol::ZirconHandle::Tag::Empty: {
        return overnet::Status(overnet::StatusCode::INVALID_ARGUMENT,
                               "Bad handle type");
      } break;
    }
  }

  if (handle_objects.size() > ZX_CHANNEL_MAX_MSG_HANDLES) {
    return overnet::Status(overnet::StatusCode::INVALID_ARGUMENT,
                           "Too many handles");
  }

  std::vector<zx_handle_t> handles;
  for (auto& h : handle_objects) {
    handles.push_back(h.release());
  }

  fidl::Message out(
      fidl::BytePart(message->bytes.data(), message->bytes.size(),
                     message->bytes.size()),
      fidl::HandlePart(handles.data(), handles.size(), handles.size()));

  if (!out.has_header()) {
    return overnet::Status(overnet::StatusCode::INVALID_ARGUMENT,
                           "Message has no header");
  }

  return then(std::move(out));
}

}  // namespace overnetstack