// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <type_traits>

#include <hwreg/internal.h>

namespace hwreg {

// This can be passed to ReadFrom and WriteTo methods.  The RegisterAddr
// object holds an offset from an MMIO base address stored in this object.
//
// The template parameter gives a factor applied to the offset before it's
// added to the base address.  This is used when mapping pio to mmio.
// For normal mmio, the unscaled RegisterMmio specialization is normally used.

template <uint32_t Scale>
class RegisterMmioScaled {
 public:
  RegisterMmioScaled(volatile void* mmio) : mmio_(reinterpret_cast<uintptr_t>(mmio)) {}
  RegisterMmioScaled(const RegisterMmioScaled& other) : mmio_(other.mmio_) {}

  // Write |val| to the |sizeof(IntType)| byte field located |offset| bytes from
  // |base()|.
  template <class IntType>
  void Write(IntType val, uint32_t offset) {
    static_assert(internal::IsSupportedInt<IntType>::value, "unsupported register access width");
    auto ptr = reinterpret_cast<volatile IntType*>(mmio_ + (offset * Scale));
    *ptr = val;
  }

  // Read the value of the |sizeof(IntType)| byte field located |offset| bytes from
  // |base()|.
  template <class IntType>
  IntType Read(uint32_t offset) {
    static_assert(internal::IsSupportedInt<IntType>::value, "unsupported register access width");
    auto ptr = reinterpret_cast<volatile IntType*>(mmio_ + (offset * Scale));
    return *ptr;
  }

  uintptr_t base() const { return mmio_; }

 private:
  const uintptr_t mmio_;
};

using RegisterMmio = RegisterMmioScaled<1>;
static_assert(std::is_copy_constructible_v<RegisterMmio>);

}  // namespace hwreg
