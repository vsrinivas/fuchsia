// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/errors.h>
#include <lib/fidl/llcpp/message.h>
#include <stdio.h>

namespace fidl {

namespace internal {

FidlMessage::FidlMessage(uint8_t* bytes, uint32_t byte_capacity, uint32_t byte_actual,
                         zx_handle_t* handles, uint32_t handle_capacity, uint32_t handle_actual)
    : ::fidl::Result(ZX_OK, nullptr),
      bytes_(bytes, byte_capacity, byte_actual),
      handles_(handles, handle_capacity, handle_actual) {
  if (byte_capacity < byte_actual) {
    SetResult(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::kErrorRequestBufferTooSmall);
  }
}

void FidlMessage::LinearizeAndEncode(const fidl_type_t* message_type, void* data) {
  ZX_DEBUG_ASSERT(!linearized_);
  if (status_ == ZX_OK) {
    uint32_t num_bytes_actual;
    uint32_t num_handles_actual;
    status_ = fidl_linearize_and_encode(message_type, data, bytes_.data(), bytes_.capacity(),
                                        handles_.data(), handles_.capacity(), &num_bytes_actual,
                                        &num_handles_actual, &error_);
    if (status_ == ZX_OK) {
      bytes_.set_actual(num_bytes_actual);
      handles_.set_actual(num_handles_actual);
    }
    linearized_ = true;
    encoded_ = true;
  }
}

void FidlMessage::Write(zx_handle_t channel) {
  ZX_DEBUG_ASSERT(encoded_);
  if (status_ != ZX_OK) {
    return;
  }
  status_ = zx_channel_write(channel, 0, bytes_.data(), bytes_.actual(), handles_.data(),
                             handles_.actual());
  if (status_ != ZX_OK) {
    error_ = ::fidl::kErrorWriteFailed;
  }
  ReleaseHandles();
}

void FidlMessage::Call(const fidl_type_t* response_type, zx_handle_t channel, uint8_t* result_bytes,
                       uint32_t result_capacity, zx_time_t deadline) {
  ZX_DEBUG_ASSERT(encoded_);
  if (status_ != ZX_OK) {
    return;
  }
  zx_handle_t result_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_num_bytes = 0u;
  uint32_t actual_num_handles = 0u;
  zx_channel_call_args_t args = {.wr_bytes = bytes().data(),
                                 .wr_handles = handles().data(),
                                 .rd_bytes = result_bytes,
                                 .rd_handles = result_handles,
                                 .wr_num_bytes = bytes().actual(),
                                 .wr_num_handles = handles().actual(),
                                 .rd_num_bytes = result_capacity,
                                 .rd_num_handles = ZX_CHANNEL_MAX_MSG_HANDLES};

  status_ = zx_channel_call(channel, 0u, deadline, &args, &actual_num_bytes, &actual_num_handles);
  if (status_ == ZX_OK) {
    status_ = fidl_decode(response_type, result_bytes, actual_num_bytes, result_handles,
                          actual_num_handles, &error_);
  } else {
    error_ = ::fidl::kErrorWriteFailed;
  }
  ReleaseHandles();
}

::fidl::Result FidlMessage::Write(::fidl::internal::ClientBase* client,
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

bool TryDispatch(void* impl, fidl_msg_t* msg, ::fidl::Transaction* txn, MethodEntry* begin,
                 MethodEntry* end) {
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  while (begin < end) {
    if (hdr->ordinal == begin->ordinal) {
      const char* error_message;
      zx_status_t status = fidl_decode(begin->type, msg->bytes, msg->num_bytes, msg->handles,
                                       msg->num_handles, &error_message);
      if (status != ZX_OK) {
        txn->InternalError({::fidl::UnbindInfo::kDecodeError, status});
      } else {
        begin->dispatch(impl, msg->bytes, txn);
      }
      return true;
    }
    ++begin;
  }
  return false;
}

}  // namespace internal

}  // namespace fidl
