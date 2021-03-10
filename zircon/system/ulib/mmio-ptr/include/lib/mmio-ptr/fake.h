// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MMIO_PTR_FAKE_H_
#define LIB_MMIO_PTR_FAKE_H_

#include <lib/mmio-ptr/mmio-ptr.h>

/// Create a fake MMIO_PTR from a regular pointer. Mock tests for drivers should
/// use this to implcitly specify that they are handing MMIO pointers.
/// Example usage:
///
///   void CheckBuffer(uint8_t *buffer) {
///     MMIO_PTR unt8_t* value_ptr = FakeMmioPtr(buffer);
///
///     // Perform reads/writes with the fake MMIO pointer
///     ASSERT_EQ(MmioRead8(&value_ptr[2]), 10);
///   }
template <typename T>
constexpr inline MMIO_PTR auto* FakeMmioPtr(T* ptr) {
  return (MMIO_PTR T*)ptr;
}

#endif  // LIB_MMIO_PTR_FAKE_H_
