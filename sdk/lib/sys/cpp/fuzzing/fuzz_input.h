// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstddef>
#include <cstdint>

namespace fuzzing {

// A C++ wrapper class for arbitrary input bytes supplied to a fuzzer.
//
// API supports zero-copy and single-copy methods for extracting data, both of
// which can fail.
class FuzzInput {
public:
  FuzzInput(const uint8_t* data, size_t remaining)
    : data_(data), remaining_(remaining) {}

  // Consumes exactly |size| bytes of underlying data without copying. Returns
  // a pointer to bytes on success or |nullptr| on failure.
  const uint8_t* TakeBytes(size_t size);

  // Copies the next |sizeof(T)| bytes of underlying data into |out|. Returns
  // |true| on success or |false| on failure. In either case, client should
  // assume that data pointed to by |out| may have been modified.
  template <typename T>
  bool CopyObject(T* out) {
    uint8_t* out_buf = reinterpret_cast<uint8_t*>(out);
    return CopyBytes(out_buf, sizeof(T));
  }

  // Copy exactly |size| bytes of underlying data into |out|. Returns |true| on
  // success or |false| on failure. In either case, client should assume that
  // data pointed to by |out| may have been modified.
  bool CopyBytes(uint8_t* out, size_t size);

private:
  // Unowned raw pointer to remaining data in fuzzing input data. It is assumed
  // that the fuzzing input buffer will outlive this |FuzzInput| instance.
  const uint8_t* data_;
  // Size of remaining data pointed to by |data_|, in bytes.
  size_t remaining_;

  // Disallow implicit constructors.
  FuzzInput() = delete;
  FuzzInput(const FuzzInput&) = delete;
  FuzzInput(FuzzInput&&) = delete;
  FuzzInput& operator=(const FuzzInput&) = delete;
  FuzzInput& operator=(FuzzInput&&) = delete;
};

}  // namespace fuzzing
