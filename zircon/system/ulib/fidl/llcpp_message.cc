// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/errors.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/trace.h>

#ifdef __Fuchsia__
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/server.h>
#include <zircon/syscalls.h>
#endif  // __Fuchsia__

#include <zircon/assert.h>

namespace fidl {

OutgoingMessage OutgoingMessage::FromEncodedCMessage(const fidl_outgoing_msg_t* c_msg) {
  return OutgoingMessage(c_msg);
}

OutgoingMessage::OutgoingMessage(const fidl_outgoing_msg_t* c_msg)
    : ::fidl::Result(ZX_OK, nullptr) {
  ZX_ASSERT(c_msg);
  switch (c_msg->type) {
    case FIDL_OUTGOING_MSG_TYPE_IOVEC: {
      message_ = *c_msg;
      iovec_capacity_ = c_msg->iovec.num_iovecs;
      handle_capacity_ = c_msg->iovec.num_handles;
      break;
    }
    case FIDL_OUTGOING_MSG_TYPE_BYTE: {
      backing_buffer_ = reinterpret_cast<uint8_t*>(c_msg->byte.bytes);
      backing_buffer_capacity_ = c_msg->byte.num_bytes;
      converted_byte_message_iovec_ = {
          .buffer = backing_buffer_,
          .capacity = backing_buffer_capacity_,
          .reserved = 0,
      };
      message_ = {
          .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
          .iovec =
              {
                  .iovecs = &converted_byte_message_iovec_,
                  .num_iovecs = 1,
                  .handles = c_msg->byte.handles,
                  .num_handles = c_msg->byte.num_handles,
              },
      };
      iovec_capacity_ = 1;
      handle_capacity_ = c_msg->byte.num_handles;
      break;
    }
    default:
      ZX_PANIC("unhandled FIDL outgoing message type");
  }
}

OutgoingMessage::OutgoingMessage(ConstructorArgs args)
    : ::fidl::Result(ZX_OK, nullptr),
      message_({
          .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
          .iovec =
              {.iovecs = args.iovecs, .num_iovecs = 0, .handles = args.handles, .num_handles = 0},
      }),
      iovec_capacity_(args.iovec_capacity),
      handle_capacity_(args.handle_capacity),
      backing_buffer_capacity_(args.backing_buffer_capacity),
      backing_buffer_(args.backing_buffer) {}

OutgoingMessage::~OutgoingMessage() {
#ifdef __Fuchsia__
  if (handle_actual() > 0) {
    FidlHandleDispositionCloseMany(handles(), handle_actual());
  }
#else
  ZX_ASSERT(handle_actual() == 0);
#endif
}

bool OutgoingMessage::BytesMatch(const OutgoingMessage& other) const {
  uint32_t iovec_index = 0, other_iovec_index = 0;
  uint32_t byte_index = 0, other_byte_index = 0;
  while (iovec_index < iovec_actual() && other_iovec_index < other.iovec_actual()) {
    zx_channel_iovec_t cur_iovec = iovecs()[iovec_index];
    zx_channel_iovec_t other_cur_iovec = other.iovecs()[other_iovec_index];
    const uint8_t* cur_bytes = reinterpret_cast<const uint8_t*>(cur_iovec.buffer);
    const uint8_t* other_cur_bytes = reinterpret_cast<const uint8_t*>(other_cur_iovec.buffer);

    uint32_t cmp_len =
        std::min(cur_iovec.capacity - byte_index, other_cur_iovec.capacity - other_byte_index);
    if (memcmp(&cur_bytes[byte_index], &other_cur_bytes[other_byte_index], cmp_len) != 0) {
      return false;
    }

    byte_index += cmp_len;
    if (byte_index == cur_iovec.capacity) {
      iovec_index++;
      byte_index = 0;
    }
    other_byte_index += cmp_len;
    if (other_byte_index == other_cur_iovec.capacity) {
      other_iovec_index++;
      other_byte_index = 0;
    }
  }
  return iovec_index == iovec_actual() && other_iovec_index == other.iovec_actual() &&
         byte_index == 0 && other_byte_index == 0;
}

void OutgoingMessage::EncodeImpl(const fidl_type_t* message_type, void* data) {
  if (status_ != ZX_OK) {
    return;
  }
  uint32_t num_iovecs_actual;
  uint32_t num_handles_actual;
  status_ =
      fidl::internal::EncodeIovecEtc(message_type, data, iovecs(), iovec_capacity(), handles(),
                                     handle_capacity(), backing_buffer(), backing_buffer_capacity(),
                                     &num_iovecs_actual, &num_handles_actual, &error_);
  if (status_ != ZX_OK) {
    return;
  }
  iovec_message().num_iovecs = num_iovecs_actual;
  iovec_message().num_handles = num_handles_actual;
}

#ifdef __Fuchsia__
void OutgoingMessage::WriteImpl(zx_handle_t channel) {
  if (status_ != ZX_OK) {
    return;
  }
  status_ = zx_channel_write_etc(channel, ZX_CHANNEL_WRITE_USE_IOVEC, iovecs(), iovec_actual(),
                                 handles(), handle_actual());
  if (status_ != ZX_OK) {
    error_ = ::fidl::kErrorWriteFailed;
  }
  ReleaseHandles();
}

::fidl::Result OutgoingMessage::Write(::fidl::internal::ClientBase* client,
                                      ::fidl::internal::ResponseContext* context) {
  if (auto channel = client->GetChannel()) {
    Write(channel->handle());
  } else {
    status_ = ZX_ERR_CANCELED;
    error_ = ::fidl::kErrorChannelUnbound;
  }
  if (!ok()) {
    client->ForgetAsyncTxn(context);
    context->OnError();
  }
  return ::fidl::Result(status_, error_);
}

void OutgoingMessage::CallImpl(const fidl_type_t* response_type, zx_handle_t channel,
                               uint8_t* result_bytes, uint32_t result_capacity,
                               zx_time_t deadline) {
  if (status_ != ZX_OK) {
    return;
  }
  zx_handle_info_t result_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_num_bytes = 0u;
  uint32_t actual_num_handles = 0u;
  zx_channel_call_etc_args_t args = {.wr_bytes = iovecs(),
                                     .wr_handles = handles(),
                                     .rd_bytes = result_bytes,
                                     .rd_handles = result_handles,
                                     .wr_num_bytes = iovec_actual(),
                                     .wr_num_handles = handle_actual(),
                                     .rd_num_bytes = result_capacity,
                                     .rd_num_handles = ZX_CHANNEL_MAX_MSG_HANDLES};

  status_ = zx_channel_call_etc(channel, ZX_CHANNEL_WRITE_USE_IOVEC, deadline, &args,
                                &actual_num_bytes, &actual_num_handles);
  if (status_ == ZX_OK) {
    status_ = fidl_decode_etc(response_type, result_bytes, actual_num_bytes, result_handles,
                              actual_num_handles, &error_);
  } else {
    error_ = ::fidl::kErrorWriteFailed;
  }
  ReleaseHandles();
}
#endif

OutgoingMessage::CopiedBytes::CopiedBytes(const OutgoingMessage& msg) {
  uint32_t byte_count = 0;
  for (uint32_t i = 0; i < msg.iovec_actual(); ++i) {
    byte_count += msg.iovecs()[i].capacity;
  }
  bytes_.reserve(byte_count);
  for (uint32_t i = 0; i < msg.iovec_actual(); ++i) {
    zx_channel_iovec_t iovec = msg.iovecs()[i];
    const uint8_t* buf_bytes = reinterpret_cast<const uint8_t*>(iovec.buffer);
    bytes_.insert(bytes_.end(), buf_bytes, buf_bytes + iovec.capacity);
  }
}

namespace internal {

IncomingMessage::IncomingMessage() : ::fidl::Result(ZX_OK, nullptr) {}

IncomingMessage::IncomingMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_info_t* handles,
                                 uint32_t handle_actual)
    : ::fidl::Result(ZX_OK, nullptr),
      message_{.bytes = bytes,
               .handles = handles,
               .num_bytes = byte_actual,
               .num_handles = handle_actual} {}

IncomingMessage::~IncomingMessage() { FidlHandleInfoCloseMany(handles(), handle_actual()); }

void IncomingMessage::Decode(const fidl_type_t* message_type) {
  fidl_trace(WillLLCPPDecode, message_type, bytes(), byte_actual(), handle_actual());
  status_ = fidl_decode_msg(message_type, &message_, &error_);
  fidl_trace(DidLLCPPDecode);
  ReleaseHandles();
}

}  // namespace internal

OutgoingToIncomingMessageResult OutgoingToIncomingMessage(OutgoingMessage& input) {
  zx_handle_disposition_t* handles = input.handles();
  uint32_t num_handles = input.handle_actual();
  input.ReleaseHandles();

  if (num_handles > ZX_CHANNEL_MAX_MSG_HANDLES) {
    FidlHandleDispositionCloseMany(handles, num_handles);
    return OutgoingToIncomingMessageResult({}, ZX_ERR_OUT_OF_RANGE, {}, nullptr);
  }

  auto buf_handles = std::make_unique<zx_handle_info_t[]>(ZX_CHANNEL_MAX_MSG_HANDLES);
  zx_status_t status = FidlHandleDispositionsToHandleInfos(handles, buf_handles.get(), num_handles);
  if (status != ZX_OK) {
    return OutgoingToIncomingMessageResult({}, status, {}, nullptr);
  }

  auto buf_bytes = input.CopyBytes();
  if (buf_bytes.size() > ZX_CHANNEL_MAX_MSG_BYTES) {
    FidlHandleDispositionCloseMany(handles, num_handles);
    return OutgoingToIncomingMessageResult({}, ZX_ERR_OUT_OF_RANGE, {}, nullptr);
  }

  return OutgoingToIncomingMessageResult(
      {
          .bytes = buf_bytes.data(),
          .handles = buf_handles.get(),
          .num_bytes = static_cast<uint32_t>(buf_bytes.size()),
          .num_handles = num_handles,
      },
      ZX_OK, std::move(buf_bytes), std::move(buf_handles));
}

OutgoingToIncomingMessageResult::OutgoingToIncomingMessageResult(
    OutgoingToIncomingMessageResult&& to_move) noexcept {
  // struct copy
  incoming_message_ = to_move.incoming_message_;
  // Prevent to_move from deleting handles.
  to_move.incoming_message_.num_handles = 0;

  status_ = to_move.status_;

  buf_bytes_ = std::move(to_move.buf_bytes_);
  buf_handles_ = std::move(to_move.buf_handles_);
}

OutgoingToIncomingMessageResult::~OutgoingToIncomingMessageResult() {
  // Ensure handles are closed before handle array is freed.
  FidlHandleInfoCloseMany(incoming_message_.handles, incoming_message_.num_handles);
  buf_bytes_ = {};
  buf_handles_ = nullptr;
}

}  // namespace fidl
