// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/decoder.h"

#include <utility>

namespace fidl {

Decoder::Decoder(Message message) : message_(std::move(message)) {}

Decoder::~Decoder() = default;

size_t Decoder::GetOffset(void* ptr) {
  return GetOffset(reinterpret_cast<uintptr_t>(ptr));
}

size_t Decoder::GetOffset(uintptr_t ptr) {
  // The |ptr| value comes from the message buffer, which we've already
  // validated. That means it should coorespond to a valid offset within the
  // message.
  return ptr - reinterpret_cast<uintptr_t>(message_.bytes().data());
}

#ifdef __Fuchsia__
void Decoder::DecodeHandle(zx::object_base* value, size_t offset) {
  zx_handle_t* handle = GetPtr<zx_handle_t>(offset);
  value->reset(*handle);
  *handle = ZX_HANDLE_INVALID;
}
#endif

uint8_t* Decoder::InternalGetPtr(size_t offset) {
  return message_.bytes().data() + offset;
}

}  // namespace fidl
