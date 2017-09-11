// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/message.h"

#include <stdlib.h>

#include <algorithm>

#include "lib/fxl/logging.h"

namespace fidl {

Message::Message() {
  Initialize();
}

Message::~Message() {
  FreeDataAndCloseHandles();
}

void Message::Reset() {
  FreeDataAndCloseHandles();

  handles_.clear();
  Initialize();
}

void Message::AllocData(uint32_t num_bytes) {
  FXL_DCHECK(!data_);
  data_num_bytes_ = num_bytes;
  data_ = static_cast<internal::MessageData*>(calloc(num_bytes, 1));
}

void Message::AllocUninitializedData(uint32_t num_bytes) {
  FXL_DCHECK(!data_);
  data_num_bytes_ = num_bytes;
  data_ = static_cast<internal::MessageData*>(malloc(num_bytes));
}

void Message::MoveTo(Message* destination) {
  FXL_DCHECK(this != destination);

  destination->FreeDataAndCloseHandles();

  // No copy needed.
  destination->data_num_bytes_ = data_num_bytes_;
  destination->data_ = data_;
  std::swap(destination->handles_, handles_);

  handles_.clear();
  Initialize();
}

void Message::Initialize() {
  data_num_bytes_ = 0;
  data_ = nullptr;
}

void Message::FreeDataAndCloseHandles() {
  free(data_);

  for (std::vector<mx_handle_t>::iterator it = handles_.begin();
       it != handles_.end(); ++it) {
    if (*it)
      mx_handle_close(*it);
  }
}

mx_status_t ReadMessage(const mx::channel& handle, Message* message) {
  FXL_DCHECK(handle);
  FXL_DCHECK(message);
  FXL_DCHECK(message->handles()->empty());
  FXL_DCHECK(message->data_num_bytes() == 0);

  uint32_t num_bytes = 0;
  uint32_t num_handles = 0;
  mx_status_t rv =
      handle.read(0, nullptr, 0, &num_bytes, nullptr, 0, &num_handles);
  if (rv != MX_ERR_BUFFER_TOO_SMALL)
    return rv;

  message->AllocUninitializedData(num_bytes);
  message->mutable_handles()->resize(num_handles);

  uint32_t num_bytes_actual = num_bytes;
  uint32_t num_handles_actual = num_handles;
  rv = handle.read(0, message->mutable_data(), num_bytes, &num_bytes_actual,
                   message->mutable_handles()->empty()
                       ? nullptr
                       : reinterpret_cast<mx_handle_t*>(
                             &message->mutable_handles()->front()),
                   num_handles, &num_handles_actual);

  FXL_DCHECK(num_bytes == num_bytes_actual);
  FXL_DCHECK(num_handles == num_handles_actual);

  return rv;
}

mx_status_t ReadAndDispatchMessage(const mx::channel& handle,
                                   MessageReceiver* receiver,
                                   bool* receiver_result) {
  Message message;
  mx_status_t rv = ReadMessage(handle, &message);
  if (receiver && rv == MX_OK)
    *receiver_result = receiver->Accept(&message);

  return rv;
}

}  // namespace fidl
