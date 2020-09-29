// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/errors.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/server.h>
#include <stdio.h>

namespace fidl {

FidlMessage::FidlMessage(uint8_t* bytes, uint32_t byte_capacity, uint32_t byte_actual,
                         zx_handle_t* handles, uint32_t handle_capacity, uint32_t handle_actual)
    : ::fidl::Result(ZX_OK, nullptr),
      message_{.bytes = bytes,
               .handles = handles,
               .num_bytes = byte_actual,
               .num_handles = handle_actual},
      byte_capacity_(byte_capacity),
      handle_capacity_(handle_capacity) {
  if (byte_capacity < byte_actual) {
    SetResult(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::kErrorRequestBufferTooSmall);
  }
}

FidlMessage::~FidlMessage() {
  if (handle_actual() > 0) {
    zx_handle_close_many(handles(), handle_actual());
  }
}

void FidlMessage::LinearizeAndEncode(const fidl_type_t* message_type, void* data) {
  if (status_ == ZX_OK) {
    uint32_t num_bytes_actual;
    uint32_t num_handles_actual;
    status_ = fidl_linearize_and_encode(message_type, data, bytes(), byte_capacity(),
                                        message_.handles, handle_capacity(), &num_bytes_actual,
                                        &num_handles_actual, &error_);
    if (status_ == ZX_OK) {
      message_.num_bytes = num_bytes_actual;
      message_.num_handles = num_handles_actual;
    }
  }
}

void FidlMessage::Decode(const fidl_type_t* message_type) {
  status_ = fidl_decode(message_type, bytes(), byte_actual(), handles(), handle_actual(), &error_);
  ReleaseHandles();
}

void FidlMessage::Write(zx_handle_t channel) {
  if (status_ != ZX_OK) {
    return;
  }
  status_ = zx_channel_write(channel, 0, bytes(), byte_actual(), handles(), handle_actual());
  if (status_ != ZX_OK) {
    error_ = ::fidl::kErrorWriteFailed;
  }
  ReleaseHandles();
}

void FidlMessage::Call(const fidl_type_t* response_type, zx_handle_t channel, uint8_t* result_bytes,
                       uint32_t result_capacity, zx_time_t deadline) {
  if (status_ != ZX_OK) {
    return;
  }
  zx_handle_t result_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_num_bytes = 0u;
  uint32_t actual_num_handles = 0u;
  zx_channel_call_args_t args = {.wr_bytes = bytes(),
                                 .wr_handles = handles(),
                                 .rd_bytes = result_bytes,
                                 .rd_handles = result_handles,
                                 .wr_num_bytes = byte_actual(),
                                 .wr_num_handles = handle_actual(),
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

}  // namespace fidl
