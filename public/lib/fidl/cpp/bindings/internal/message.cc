// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/message.h"

#include <stdlib.h>
#include <zircon/assert.h>

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
  ZX_DEBUG_ASSERT(!data_);
  data_num_bytes_ = num_bytes;
  data_ = static_cast<internal::MessageData*>(calloc(num_bytes, 1));
}

void AllocMessage::AllocUninitializedData(uint32_t num_bytes) {
  ZX_DEBUG_ASSERT(!data_);
  data_num_bytes_ = num_bytes;
  data_ = static_cast<internal::MessageData*>(malloc(num_bytes));
}

void AllocMessage::CopyDataFrom(Message* source) {
  AllocUninitializedData(source->data_num_bytes());
  memcpy(mutable_data(), source->data(), source->data_num_bytes());
}

void AllocMessage::MoveFrom(AllocMessage* source) {
  ZX_DEBUG_ASSERT(this != source);

  // Move the data.  No copying is needed.
  free(data_);
  data_num_bytes_ = source->data_num_bytes_;
  data_ = source->data_;
  source->data_num_bytes_ = 0;
  source->data_ = nullptr;

  // Move the handles.
  MoveHandlesFrom(source);
}

PreallocMessage::~PreallocMessage() {
  if (data_ != reinterpret_cast<internal::MessageData*>(prealloc_buf_))
    free(data_);
}

void PreallocMessage::AllocUninitializedData(uint32_t num_bytes) {
  ZX_DEBUG_ASSERT(!data_);
  if (num_bytes <= sizeof(prealloc_buf_)) {
    data_ = reinterpret_cast<internal::MessageData*>(prealloc_buf_);
  } else {
    data_ = reinterpret_cast<internal::MessageData*>(malloc(num_bytes));
  }
  data_num_bytes_ = num_bytes;
}

zx_status_t PreallocMessage::ReadMessage(const zx::channel& channel) {
  ZX_DEBUG_ASSERT(channel);
  ZX_DEBUG_ASSERT(handles()->empty());
  ZX_DEBUG_ASSERT(!data());
  ZX_DEBUG_ASSERT(data_num_bytes() == 0);

  // If the message fits into prealloc_buf_ and contains no handles, we
  // will only need one call to channel.read().  Otherwise, we will need
  // the second call to channel.read().
  uint32_t num_bytes = 0;
  uint32_t num_handles = 0;
  zx_status_t rv =
      channel.read(0, prealloc_buf_, sizeof(prealloc_buf_), &num_bytes, nullptr,
                   0, &num_handles);
  if (rv == ZX_OK) {
    data_ = reinterpret_cast<internal::MessageData*>(prealloc_buf_);
    data_num_bytes_ = num_bytes;
    ZX_DEBUG_ASSERT(num_handles == 0);
    return ZX_OK;
  }
  if (rv != ZX_ERR_BUFFER_TOO_SMALL)
    return rv;

  AllocUninitializedData(num_bytes);
  mutable_handles()->resize(num_handles);

  uint32_t num_bytes_actual = num_bytes;
  uint32_t num_handles_actual = num_handles;
  rv = channel.read(0, mutable_data(), num_bytes, &num_bytes_actual,
                   mutable_handles()->empty()
                       ? nullptr
                       : reinterpret_cast<zx_handle_t*>(
                             &mutable_handles()->front()),
                   num_handles, &num_handles_actual);

  ZX_DEBUG_ASSERT(num_bytes == num_bytes_actual);
  ZX_DEBUG_ASSERT(num_handles == num_handles_actual);

  return rv;
}

zx_status_t ReadAndDispatchMessage(const zx::channel& channel,
                                   MessageReceiver* receiver,
                                   bool* receiver_result) {
  PreallocMessage message;
  zx_status_t rv = message.ReadMessage(channel);
  if (receiver && rv == ZX_OK)
    *receiver_result = receiver->Accept(&message);

  return rv;
}

zx_status_t WriteMessage(const zx::channel& channel, Message* message) {
  ZX_DEBUG_ASSERT(channel);
  ZX_DEBUG_ASSERT(message);

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

  ZX_DEBUG_ASSERT(response);
  zx_status_t status = WriteMessage(channel, message);
  if (status != ZX_OK)
    return status;

  zx_signals_t observed;
  status = channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                            zx::time::infinite(), &observed);

  if (status != ZX_OK)
    return status;

  if (observed & ZX_CHANNEL_READABLE)
    return response->ReadMessage(channel);

  ZX_DEBUG_ASSERT(observed & ZX_CHANNEL_PEER_CLOSED);
  return ZX_ERR_PEER_CLOSED;
}

}  // namespace fidl
