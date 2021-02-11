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

OutgoingMessage::~OutgoingMessage() {
#ifdef __Fuchsia__
  if (handle_actual() > 0) {
    FidlHandleDispositionCloseMany(handles(), handle_actual());
  }
#else
  ZX_ASSERT(handle_actual() == 0);
#endif
}

#ifdef __Fuchsia__
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
#endif

OutgoingByteMessage::OutgoingByteMessage(uint8_t* bytes, uint32_t byte_capacity,
                                         uint32_t byte_actual, zx_handle_disposition_t* handles,
                                         uint32_t handle_capacity, uint32_t handle_actual)
    : OutgoingMessage(
          {
              .type = FIDL_OUTGOING_MSG_TYPE_BYTE,
              .byte = {.bytes = bytes,
                       .handles = handles,
                       .num_bytes = byte_actual,
                       .num_handles = handle_actual},
          },
          handle_capacity),
      byte_capacity_(byte_capacity) {
  if (byte_capacity < byte_actual) {
    SetResult(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::kErrorRequestBufferTooSmall);
  }
  if (handle_capacity < handle_actual) {
    SetResult(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::kErrorRequestBufferTooSmall);
  }
}

void OutgoingByteMessage::EncodeImpl(const fidl_type_t* message_type, void* data) {
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
void OutgoingByteMessage::WriteImpl(zx_handle_t channel) {
  if (status_ != ZX_OK) {
    return;
  }
  status_ = zx_channel_write_etc(channel, 0, bytes(), byte_actual(), handles(), handle_actual());
  if (status_ != ZX_OK) {
    error_ = ::fidl::kErrorWriteFailed;
  }
  ReleaseHandles();
}

void OutgoingByteMessage::CallImpl(const fidl_type_t* response_type, zx_handle_t channel,
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

OutgoingIovecMessage::OutgoingIovecMessage(constructor_args args)
    : OutgoingMessage(
          {
              .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
              .iovec =
                  {
                      .iovecs = args.iovecs,
                      .num_iovecs = args.iovecs_actual,
                      .handles = args.handles,
                      .num_handles = args.handle_actual,
                  },
          },
          args.handle_capacity),
      iovecs_capacity_(args.iovecs_capacity),
      substitutions_(args.substitutions),
      substitutions_capacity_(args.substitutions_capacity),
      substitutions_actual_(args.substitutions_actual) {
  if (args.iovecs_capacity < args.iovecs_actual) {
    SetResult(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::kErrorRequestBufferTooSmall);
  }
  if (args.substitutions_capacity < args.substitutions_actual) {
    SetResult(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::kErrorRequestBufferTooSmall);
  }
  if (args.handle_capacity < args.handle_actual) {
    SetResult(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::kErrorRequestBufferTooSmall);
  }
}
OutgoingIovecMessage::~OutgoingIovecMessage() { PatchSubstitutions(); }

void OutgoingIovecMessage::EncodeImpl(const fidl_type_t* message_type, void* data) {
  if (status_ != ZX_OK) {
    return;
  }
  PatchSubstitutions();
  uint32_t num_iovecs_actual;
  uint32_t num_substitutions_actual;
  uint32_t num_handles_actual;
  status_ = fidl_encode_iovec_etc(message_type, data, iovecs(), iovecs_capacity_, substitutions_,
                                  substitutions_capacity_, handles(), handle_capacity(),
                                  &num_iovecs_actual, &num_substitutions_actual,
                                  &num_handles_actual, &error_);
  if (status_ == ZX_OK) {
    message()->iovec.num_iovecs = num_iovecs_actual;
    substitutions_actual_ = num_substitutions_actual;
    message()->iovec.num_handles = num_handles_actual;
  }
}

#ifdef __Fuchsia__
void OutgoingIovecMessage::WriteImpl(zx_handle_t channel) {
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

void OutgoingIovecMessage::CallImpl(const fidl_type_t* response_type, zx_handle_t channel,
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

void OutgoingIovecMessage::PatchSubstitutions() {
  for (uint32_t i = 0; i < substitutions_actual_; i++) {
    *substitutions_[i].ptr = substitutions_[i].value;
  }
  substitutions_actual_ = 0;
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
  status_ =
      fidl_decode_etc(message_type, bytes(), byte_actual(), handles(), handle_actual(), &error_);
  ReleaseHandles();
}

}  // namespace internal

OutgoingToIncomingMessageResult OutgoingToIncomingMessage(OutgoingMessage& input) {
  fidl_outgoing_msg_t* outgoing_msg = input.message();
  zx_handle_disposition_t* handles;
  uint32_t num_handles;
  switch (outgoing_msg->type) {
    case FIDL_OUTGOING_MSG_TYPE_BYTE: {
      handles = outgoing_msg->byte.handles;
      num_handles = outgoing_msg->byte.num_handles;
      break;
    }
    case FIDL_OUTGOING_MSG_TYPE_IOVEC: {
      handles = outgoing_msg->iovec.handles;
      num_handles = outgoing_msg->iovec.num_handles;
      break;
    }
    default:
      ZX_PANIC("unknown message type");
  }
  input.ReleaseHandles();

  if (num_handles > ZX_CHANNEL_MAX_MSG_HANDLES) {
    FidlHandleDispositionCloseMany(handles, num_handles);
    return OutgoingToIncomingMessageResult({}, ZX_ERR_OUT_OF_RANGE, nullptr, nullptr);
  }

  uint32_t num_bytes;
  switch (outgoing_msg->type) {
    case FIDL_OUTGOING_MSG_TYPE_BYTE: {
      num_bytes = outgoing_msg->byte.num_bytes;
      break;
    }
    case FIDL_OUTGOING_MSG_TYPE_IOVEC: {
      num_bytes = 0;
      for (uint32_t i = 0; i < outgoing_msg->iovec.num_iovecs; i++) {
        num_bytes += outgoing_msg->iovec.iovecs[i].capacity;
      }
      break;
    }
    default:
      ZX_PANIC("unknown message type");
  }
  if (num_bytes > ZX_CHANNEL_MAX_MSG_BYTES) {
    FidlHandleDispositionCloseMany(handles, num_handles);
    return OutgoingToIncomingMessageResult({}, ZX_ERR_OUT_OF_RANGE, nullptr, nullptr);
  }

  std::unique_ptr<uint8_t[]> buf_bytes = std::make_unique<uint8_t[]>(num_bytes);
  switch (outgoing_msg->type) {
    case FIDL_OUTGOING_MSG_TYPE_BYTE: {
      memcpy(buf_bytes.get(), outgoing_msg->byte.bytes, num_bytes);
      break;
    }
    case FIDL_OUTGOING_MSG_TYPE_IOVEC: {
      uint32_t offset = 0;
      for (uint32_t i = 0; i < outgoing_msg->iovec.num_iovecs; i++) {
        auto iovec = outgoing_msg->iovec.iovecs[i];
        memcpy(&buf_bytes[offset], iovec.buffer, iovec.capacity);
        offset += iovec.capacity;
      }
      break;
    }
    default:
      ZX_PANIC("unknown message type");
  }

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

OutgoingToIncomingMessageResult::~OutgoingToIncomingMessageResult() {
  // Ensure handles are closed before handle array is freed.
  FidlHandleInfoCloseMany(incoming_message_.handles, incoming_message_.num_handles);
  buf_bytes_ = nullptr;
  buf_handles_ = nullptr;
}

}  // namespace fidl
