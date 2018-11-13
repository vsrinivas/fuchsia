// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/overnet/vocabulary/slice.h"
#include "garnet/lib/overnet/vocabulary/status.h"
#include "garnet/public/lib/fidl/cpp/object_coding.h"

namespace overnet {

template <class T>
StatusOr<Slice> Encode(T* object) {
  std::vector<uint8_t> output;
  const char* error_msg;
  if (zx_status_t status = fidl::EncodeObject(object, &output, &error_msg);
      status != ZX_OK) {
    return Status::FromZx(status, error_msg);
  }
  return Slice::FromContainer(std::move(output));
}

template <class T>
StatusOr<T> Decode(uint8_t* bytes, size_t length) {
  const char* error_msg;
  T out;
  if (zx_status_t status = fidl::DecodeObject(bytes, length, &out, &error_msg);
      status != ZX_OK) {
    return Status::FromZx(status, error_msg);
  }
  return out;
}

}  // namespace overnet
