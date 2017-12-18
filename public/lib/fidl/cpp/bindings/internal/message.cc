// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/message.h"

#include <stdlib.h>

#include <algorithm>

#include "lib/fxl/logging.h"

namespace fidl {

Message::Message() {
}

Message::~Message() {
  CloseHandles();
}

void Message::MoveHandlesFrom(Message* source) {
  CloseHandles();
  handles_.clear();
  std::swap(source->handles_, handles_);
}

// Closes the handles in the handles_ vector but does not remove them from
// the vector.
void Message::CloseHandles() {
  for (zx_handle_t handle : handles_) {
    if (handle != ZX_HANDLE_INVALID)
      zx_handle_close(handle);
  }
}

AllocMessage::AllocMessage() {
}

AllocMessage::~AllocMessage() {
  free(data_);
}

void AllocMessage::Reset() {
  // Reset the data.
  free(data_);
  data_num_bytes_ = 0;
  data_ = nullptr;

  // Reset the handles.
  CloseHandles();
  handles_.clear();
}

void AllocMessage::AllocData(uint32_t num_bytes) {
  FXL_DCHECK(!data_);
  data_num_bytes_ = num_bytes;
  data_ = static_cast<internal::MessageData*>(calloc(num_bytes, 1));
}

void AllocMessage::AllocUninitializedData(uint32_t num_bytes) {
  FXL_DCHECK(!data_);
  data_num_bytes_ = num_bytes;
  data_ = static_cast<internal::MessageData*>(malloc(num_bytes));
}

void AllocMessage::CopyDataFrom(Message* source) {
  AllocUninitializedData(source->data_num_bytes());
  memcpy(mutable_data(), source->data(), source->data_num_bytes());
}

void AllocMessage::MoveTo(AllocMessage* destination) {
  FXL_DCHECK(this != destination);

  // Move the data.  No copying is needed.
  free(destination->data_);
  destination->data_num_bytes_ = data_num_bytes_;
  destination->data_ = data_;
  data_num_bytes_ = 0;
  data_ = nullptr;

  // Move the handles.
  destination->MoveHandlesFrom(this);
}

PreallocMessage::~PreallocMessage() {
  if (data_ != reinterpret_cast<internal::MessageData*>(prealloc_buf_))
    free(data_);
}

void PreallocMessage::AllocUninitializedData(uint32_t num_bytes) {
  FXL_DCHECK(!data_);
  if (num_bytes <= sizeof(prealloc_buf_)) {
    data_ = reinterpret_cast<internal::MessageData*>(prealloc_buf_);
  } else {
    data_ = reinterpret_cast<internal::MessageData*>(malloc(num_bytes));
  }
  data_num_bytes_ = num_bytes;
}

zx_status_t ReadMessage(const zx::channel& channel, PreallocMessage* message) {
  FXL_DCHECK(channel);
  FXL_DCHECK(message);
  FXL_DCHECK(message->handles()->empty());
  FXL_DCHECK(message->data_num_bytes() == 0);

  uint32_t num_bytes = 0;
  uint32_t num_handles = 0;
  zx_status_t rv =
  channel.read(0, nullptr, 0, &num_bytes, nullptr, 0, &num_handles);
  if (rv != ZX_ERR_BUFFER_TOO_SMALL)
    return rv;

  message->AllocUninitializedData(num_bytes);
  message->mutable_handles()->resize(num_handles);

  uint32_t num_bytes_actual = num_bytes;
  uint32_t num_handles_actual = num_handles;
  rv = channel.read(0, message->mutable_data(), num_bytes, &num_bytes_actual,
                   message->mutable_handles()->empty()
                       ? nullptr
                       : reinterpret_cast<zx_handle_t*>(
                             &message->mutable_handles()->front()),
                   num_handles, &num_handles_actual);

  FXL_DCHECK(num_bytes == num_bytes_actual);
  FXL_DCHECK(num_handles == num_handles_actual);

  return rv;
}

zx_status_t ReadAndDispatchMessage(const zx::channel& channel,
                                   MessageReceiver* receiver,
                                   bool* receiver_result) {
  PreallocMessage message;
  zx_status_t rv = ReadMessage(channel, &message);
  if (receiver && rv == ZX_OK)
    *receiver_result = receiver->Accept(&message);

  return rv;
}

zx_status_t WriteMessage(const zx::channel& channel, Message* message) {
  FXL_DCHECK(channel);
  FXL_DCHECK(message);

  zx_status_t status = channel.write(
      0, message->data(), message->data_num_bytes(),
      message->mutable_handles()->empty()
          ? nullptr
          : reinterpret_cast<const zx_handle_t*>(
            message->mutable_handles()->data()),
      static_cast<uint32_t>(message->mutable_handles()->size()));

  if (status == ZX_OK) {
    // The handles were successfully transferred, so we don't need the message
    // to track their lifetime any longer.
    message->mutable_handles()->clear();
  }

  return status;
}

zx_status_t CallMessage(const zx::channel& channel, Message* message,
                        PreallocMessage* response) {
  // TODO(abarth): Once we convert to the FIDL2 wire format, switch this code
  // to use zx_channel_call.

  FXL_DCHECK(response);
  zx_status_t status = WriteMessage(channel, message);
  if (status != ZX_OK)
    return status;

  zx_signals_t observed;
  status = channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                            ZX_TIME_INFINITE, &observed);

  if (status != ZX_OK)
    return status;

  if (observed & ZX_CHANNEL_READABLE)
    return ReadMessage(channel, response);

  FXL_DCHECK(observed & ZX_CHANNEL_PEER_CLOSED);
  return ZX_ERR_PEER_CLOSED;
}

}  // namespace fidl
