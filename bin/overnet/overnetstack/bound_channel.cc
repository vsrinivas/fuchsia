// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bound_channel.h"
#include <zircon/assert.h>
#include "fuchsia_port.h"
#include "garnet/lib/overnet/endpoint/message_builder.h"
#include "overnet_app.h"

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

// Overnet MessageReceiver that builds up a FIDL channel message
class BoundChannel::FidlMessageBuilder final : public overnet::MessageReceiver {
 public:
  FidlMessageBuilder(BoundChannel* stream)
      : stream_(stream),
        message_(fidl::BytePart(
                     bytes_, ZX_CHANNEL_MAX_MSG_BYTES,
                     // We start with enough space just for the FIDL header
                     sizeof(fidl_message_header_t)),
                 fidl::HandlePart(handles_, ZX_CHANNEL_MAX_MSG_HANDLES)) {
    // Zero out header to start with
    message_.header().txid = 0;
    message_.header().reserved0 = 0;
    message_.header().flags = 0;
  }

  FidlMessageBuilder(const FidlMessageBuilder&) = delete;
  FidlMessageBuilder& operator=(const FidlMessageBuilder&) = delete;

  ~FidlMessageBuilder() {}

  const fidl::Message& message() const { return message_; }
  fidl::Message& message() { return message_; }

  overnet::Status SetTransactionId(uint32_t txid) override {
    message_.set_txid(txid);
    return overnet::Status::Ok();
  }

  overnet::Status SetOrdinal(uint32_t ordinal) override {
    message_.header().ordinal = ordinal;
    return overnet::Status::Ok();
  }

  overnet::Status SetBody(overnet::Slice body) override {
    // For now we copy the body into the message.
    // TODO(ctiller): consider other schemes to eliminate this copy?
    const auto new_actual = sizeof(fidl_message_header_t) + body.length();
    if (new_actual > message_.bytes().capacity()) {
      return overnet::Status(overnet::StatusCode::FAILED_PRECONDITION,
                             "Message too large");
    }
    message_.bytes().set_actual(new_actual);
    memcpy(message_.bytes().begin(), body.begin(), body.length());
    return overnet::Status::Ok();
  }

  overnet::Status AppendUnknownHandle() override { return AppendHandle(0); }

  overnet::Status AppendChannelHandle(
      overnet::RouterEndpoint::ReceivedIntroduction received_introduction)
      override;

  // Mark this message as sent, meaning that we no longer need to close
  // contained handles.
  void Sent() { message_.ClearHandlesUnsafe(); }

 private:
  overnet::Status AppendHandle(zx_handle_t hdl) {
    auto& handles = message_.handles();
    if (handles.actual() == handles.capacity()) {
      zx_handle_close(hdl);
      return overnet::Status(overnet::StatusCode::FAILED_PRECONDITION,
                             "Too many handles in message");
    }
    handles.data()[handles.actual()] = hdl;
    handles.set_actual(handles.actual() + 1);
    return overnet::Status::Ok();
  }

  BoundChannel* stream_;
  uint8_t bytes_[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles_[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl::Message message_;
};

void BoundChannel::Close(const overnet::Status& status) {
  OVERNET_TRACE(DEBUG) << "CLOSE: " << status << " closed=" << closed_;
  if (closed_) {
    return;
  }
  closed_ = true;
  zx_channel_.reset();
  if (net_recv_) {
    net_recv_->Close(status);
  }
  overnet_stream_.Close(status, [this] { delete this; });
}

void BoundChannel::WriteToChannelAndStartNextRead(
    std::unique_ptr<FidlMessageBuilder> builder) {
  OVERNET_TRACE(DEBUG) << "WriteToChannelAndStartNextRead txid="
                       << builder->message().txid()
                       << " bytes_cnt=" << builder->message().bytes().actual()
                       << " handles_cnt="
                       << builder->message().handles().actual()
                       << " hdl=" << zx_channel_.get();
  auto err = builder->message().Write(zx_channel_.get(), 0);
  switch (err) {
    case ZX_OK:
      builder->Sent();
      StartNetRead();
      break;
    case ZX_ERR_SHOULD_WAIT:
      // Kernel push back: capture the slice, and ask to be informed when we
      // can write.
      waiting_to_write_ = std::move(builder);
      err = async_begin_wait(dispatcher_, &wait_send_.wait);
      if (err != ZX_OK) {
        Close(ToOvernetStatus(err).WithContext("Beginning wait for write"));
      }
      break;
    default:
      // If the write failed, close the stream.
      Close(ToOvernetStatus(err).WithContext("Write"));
  }
}

void BoundChannel::StartChannelRead() {
  OVERNET_TRACE(DEBUG) << "StartChannelRead hdl=" << zx_channel_.get();
  uint8_t message_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl::Message message(
      fidl::BytePart(message_buffer, ZX_CHANNEL_MAX_MSG_BYTES),
      fidl::HandlePart(handles, ZX_CHANNEL_MAX_MSG_HANDLES));
  auto err = message.Read(zx_channel_.get(), 0);
  OVERNET_TRACE(DEBUG) << "StartChannelRead read result: "
                       << ToOvernetStatus(err);
  switch (err) {
    case ZX_OK: {
      // Successful read: build the output message.
      OVERNET_TRACE(DEBUG) << "Convert message to overnet";
      auto send_slice = ChannelMessageToOvernet(std::move(message));
      OVERNET_TRACE(DEBUG) << "Convert message to overnet got: " << send_slice;
      if (send_slice.is_error()) {
        Close(send_slice.AsStatus());
        break;
      }
      overnet::RouterEndpoint::Stream::SendOp(&overnet_stream_,
                                              send_slice->length())
          .Push(std::move(*send_slice), [this] { StartChannelRead(); });
    } break;
    case ZX_ERR_SHOULD_WAIT:
      // Kernel push back: ask to be informed when we can try again.
      err = async_begin_wait(dispatcher_, &wait_recv_.wait);
      OVERNET_TRACE(DEBUG) << "async_begin_wait result: "
                           << ToOvernetStatus(err);
      if (err != ZX_OK) {
        Close(ToOvernetStatus(err).WithContext("Beginning wait for read"));
        break;
      }
      break;
    default:
      // If the read failed, close the stream.
      Close(ToOvernetStatus(err).WithContext("Read"));
      break;
  }
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
  OVERNET_TRACE(DEBUG) << "OnSendReady: status=" << ToOvernetStatus(status);
  WriteToChannelAndStartNextRead(std::move(waiting_to_write_));
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
  OVERNET_TRACE(DEBUG) << "OnRecvReady: status=" << ToOvernetStatus(status)
                       << " observed=" << signal->observed;

  if (status != ZX_OK) {
    Close(ToOvernetStatus(status).WithContext("OnRecvReady"));
    return;
  }

  if (signal->observed & ZX_CHANNEL_READABLE) {
    StartChannelRead();
    return;
  }

  // Note: we flush all reads before propagating the close.
  ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
  Close(overnet::Status::Ok());
}

void BoundChannel::StartNetRead() {
  OVERNET_TRACE(DEBUG) << "StartNetRead";
  net_recv_.Reset(&overnet_stream_);
  net_recv_->PullAll(
      [this](overnet::StatusOr<std::vector<overnet::Slice>> status) {
        OVERNET_TRACE(DEBUG) << "StartNetRead got " << status;
        if (status.is_error()) {
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
        auto builder = std::make_unique<FidlMessageBuilder>(this);
        auto parse_status = overnet::ParseMessageInto(
            overnet::Slice::Join(status->begin(), status->end()),
            overnet_stream_.peer(), app_->endpoint(), builder.get());
        WriteToChannelAndStartNextRead(std::move(builder));
      });
}

overnet::StatusOr<overnet::Slice> BoundChannel::ChannelMessageToOvernet(
    fidl::Message message) {
  overnet::MessageWireEncoder builder(&overnet_stream_);
  if (!message.has_header()) {
    return overnet::Status(overnet::StatusCode::FAILED_PRECONDITION,
                           "FIDL message without a header");
  }
  assert(message.flags() == 0);
  auto status =
      builder.SetTransactionId(message.txid())
          .Then([&] { return builder.SetOrdinal(message.ordinal()); })
          .Then([&] {
            return builder.SetBody(overnet::Slice::ReferencingContainer(
                message.bytes().begin(), message.bytes().end()));
          });

  // Keep track of failure.
  // We cannot leave the loop below early or we risk leaking handles.
  for (auto handle : message.handles()) {
    zx::handle hdl(handle);
    if (status.is_error()) {
      continue;
    }
    zx_info_handle_basic_t info;
    auto err = hdl.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr,
                            nullptr);
    if (err != ZX_OK) {
      status = ToOvernetStatus(err).WithContext("Getting handle type");
      continue;
    }
    switch (info.type) {
      case ZX_OBJ_TYPE_CHANNEL: {
        auto new_stream = builder.AppendChannelHandle(overnet::Introduction());
        auto channel = zx::channel(hdl.release());
        assert(channel.is_valid());
        assert(!hdl.is_valid());
        if (new_stream.is_error()) {
          status = new_stream.AsStatus();
        } else {
          app_->BindStream(std::move(*new_stream), std::move(channel));
          assert(!channel.is_valid());
        }
      } break;
      default:
        auto append_status = builder.AppendUnknownHandle().WithContext(
            "Appending unknown channel");
        if (append_status.is_error()) {
          status = append_status;
        }
        break;
    }
  }
  message.ClearHandlesUnsafe();
  if (status.is_error()) {
    return status;
  }
  return builder.Write(overnet::Border::None());
}

overnet::Status BoundChannel::FidlMessageBuilder::AppendChannelHandle(
    overnet::RouterEndpoint::ReceivedIntroduction received_introduction) {
  // TODO(ctiller): interpret received_introduction.introduction?
  zx_handle_t a, b;
  zx_status_t err = zx_channel_create(0, &a, &b);
  if (err != ZX_OK) {
    return ToOvernetStatus(err).WithContext("Appending channel");
  }
  stream_->app_->BindStream(std::move(received_introduction.new_stream),
                            zx::channel(a));
  return AppendHandle(b);
}

}  // namespace overnetstack