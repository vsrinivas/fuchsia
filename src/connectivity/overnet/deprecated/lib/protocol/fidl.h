// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fidl/cpp/object_coding.h"
#include "src/connectivity/overnet/deprecated/lib/protocol/coding.h"
#include "src/connectivity/overnet/deprecated/lib/vocabulary/slice.h"
#include "src/connectivity/overnet/deprecated/lib/vocabulary/status.h"

namespace overnet {

namespace fidl_impl {
template <class T, class CodingSelector>
StatusOr<Slice> Encode(CodingSelector coding_selector, T* object) {
  std::vector<uint8_t> output;
  const char* error_msg;
  if (zx_status_t status = fidl::EncodeObject(object, &output, &error_msg); status != ZX_OK) {
    return Status::FromZx(status, error_msg);
  }
  const auto coding = coding_selector(output.size());
  return Encode(coding, Slice::FromContainer(std::move(output)));
}

}  // namespace fidl_impl

template <class T>
StatusOr<Slice> Encode(Coding coding, T* object) {
  return fidl_impl::Encode([coding](auto) { return coding; }, object);
}

template <class T>
StatusOr<Slice> Encode(T* object) {
  return fidl_impl::Encode(
      [](size_t size) { return SliceCodingOracle().SetSize(size).SuggestCoding(); }, object);
}

template <class T>
StatusOr<T> Decode(Slice update) {
  auto decoded = Decode(std::move(update));
  if (decoded.is_error()) {
    return decoded.AsStatus();
  }
  std::vector<uint8_t> copy(decoded->begin(), decoded->end());
  const char* error_msg;
  T out;
  if (zx_status_t status = fidl::DecodeObject(copy.data(), copy.size(), &out, &error_msg);
      status != ZX_OK) {
    return Status::FromZx(status, error_msg);
  }
  return out;
}

}  // namespace overnet
