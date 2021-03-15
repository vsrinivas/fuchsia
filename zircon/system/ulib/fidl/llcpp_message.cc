// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/errors.h>
#include <lib/fidl/llcpp/message.h>
#ifdef __Fuchsia__
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/server.h>
#include <zircon/syscalls.h>
#endif
#include <zircon/assert.h>

namespace fidl {

OutgoingMessage::OutgoingMessage(const fidl_outgoing_msg_t* c_msg)
    : ::fidl::Result(ZX_OK, nullptr),
      message_(*c_msg),
      byte_capacity_(c_msg->byte.num_bytes),
      handle_capacity_(c_msg->byte.num_handles) {
  ZX_ASSERT(c_msg->type == FIDL_OUTGOING_MSG_TYPE_BYTE);
}

OutgoingMessage::OutgoingMessage(uint8_t* bytes, uint32_t byte_capacity, uint32_t byte_actual,
                                 zx_handle_disposition_t* handles, uint32_t handle_capacity,
                                 uint32_t handle_actual)
    : ::fidl::Result(ZX_OK, nullptr),
      message_({
          .type = FIDL_OUTGOING_MSG_TYPE_BYTE,
          .byte = {.bytes = bytes,
                   .handles = handles,
                   .num_bytes = byte_actual,
                   .num_handles = handle_actual},
      }),
      byte_capacity_(byte_capacity),
      handle_capacity_(handle_capacity) {
  if (byte_capacity < byte_actual) {
    SetResult(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::kErrorRequestBufferTooSmall);
  }
  if (handle_capacity < handle_actual) {
    SetResult(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::kErrorRequestBufferTooSmall);
  }
}

OutgoingMessage::~OutgoingMessage() {
#ifdef __Fuchsia__
  if (handle_actual() > 0) {
    FidlHandleDispositionCloseMany(handles(), handle_actual());
  }
#else
  ZX_ASSERT(handle_actual() == 0);
#endif
}

void OutgoingMessage::EncodeImpl(const fidl_type_t* message_type, void* data) {
  if (status_ != ZX_OK) {
    return;
  }
  uint32_t num_bytes_actual;
  uint32_t num_handles_actual;
  status_ = fidl_linearize_and_encode_etc(message_type, data, bytes(), byte_capacity_, handles(),
                                          handle_capacity(), &num_bytes_actual, &num_handles_actual,
                                          &error_);
  if (status_ == ZX_OK) {
    message()->byte.num_bytes = num_bytes_actual;
    message()->byte.num_handles = num_handles_actual;
  }
}

#ifdef __Fuchsia__
void OutgoingMessage::WriteImpl(zx_handle_t channel) {
  if (status_ != ZX_OK) {
    return;
  }
  status_ = zx_channel_write_etc(channel, 0, bytes(), byte_actual(), handles(), handle_actual());
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
    delete context;
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
  zx_channel_call_etc_args_t args = {.wr_bytes = bytes(),
                                     .wr_handles = handles(),
                                     .rd_bytes = result_bytes,
                                     .rd_handles = result_handles,
                                     .wr_num_bytes = byte_actual(),
                                     .wr_num_handles = handle_actual(),
                                     .rd_num_bytes = result_capacity,
                                     .rd_num_handles = ZX_CHANNEL_MAX_MSG_HANDLES};

  status_ =
      zx_channel_call_etc(channel, 0u, deadline, &args, &actual_num_bytes, &actual_num_handles);
  if (status_ == ZX_OK) {
    status_ = fidl_decode_etc(response_type, result_bytes, actual_num_bytes, result_handles,
                              actual_num_handles, &error_);
  } else {
    error_ = ::fidl::kErrorWriteFailed;
  }
  ReleaseHandles();
}
#endif

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
  status_ =
      fidl_decode_etc(message_type, bytes(), byte_actual(), handles(), handle_actual(), &error_);
  ReleaseHandles();
}

}  // namespace internal

OutgoingToIncomingMessageResult OutgoingToIncomingMessage(OutgoingMessage& input) {
  fidl_outgoing_msg_t* outgoing_msg = input.message();
  ZX_ASSERT(outgoing_msg->type == FIDL_OUTGOING_MSG_TYPE_BYTE);
  zx_handle_disposition_t* handles = outgoing_msg->byte.handles;
  uint32_t num_handles = outgoing_msg->byte.num_handles;
  input.ReleaseHandles();

  if (num_handles > ZX_CHANNEL_MAX_MSG_HANDLES) {
    FidlHandleDispositionCloseMany(handles, num_handles);
    return OutgoingToIncomingMessageResult({}, ZX_ERR_OUT_OF_RANGE, nullptr, nullptr);
  }

  uint32_t num_bytes = outgoing_msg->byte.num_bytes;
  if (num_bytes > ZX_CHANNEL_MAX_MSG_BYTES) {
    FidlHandleDispositionCloseMany(handles, num_handles);
    return OutgoingToIncomingMessageResult({}, ZX_ERR_OUT_OF_RANGE, nullptr, nullptr);
  }

  std::unique_ptr<uint8_t[]> buf_bytes = std::make_unique<uint8_t[]>(num_bytes);
  memcpy(buf_bytes.get(), outgoing_msg->byte.bytes, num_bytes);

  auto buf_handles = std::make_unique<zx_handle_info_t[]>(ZX_CHANNEL_MAX_MSG_HANDLES);
  zx_status_t status = FidlHandleDispositionsToHandleInfos(handles, buf_handles.get(), num_handles);
  if (status != ZX_OK) {
    return OutgoingToIncomingMessageResult({}, status, nullptr, nullptr);
  }

  return OutgoingToIncomingMessageResult(
      {
          .bytes = buf_bytes.get(),
          .handles = buf_handles.get(),
          .num_bytes = num_bytes,
          .num_handles = num_handles,
      },
      ZX_OK, std::move(buf_bytes), std::move(buf_handles));
}

OutgoingToIncomingMessageResult::OutgoingToIncomingMessageResult(
    OutgoingToIncomingMessageResult&& to_move) {
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
  buf_bytes_ = nullptr;
  buf_handles_ = nullptr;
}

}  // namespace fidl
