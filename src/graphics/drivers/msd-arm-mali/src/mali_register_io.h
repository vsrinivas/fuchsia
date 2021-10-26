// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_MALI_REGISTER_IO_H_
#define SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_MALI_REGISTER_IO_H_

#include <utility>

#include "magma_util/register_io.h"

namespace mali {

// This is a mixin that can be used to write to a RegisterIo with a
// hwreg/bitfield.h Register class. The Mali hardware only supports accesses up
// to 32 bits in size, so 64-bit accesses are converted to two non-atomic 32-bit
// accesses (little-endian).
template <typename Parent>
class RegisterIoAdapter : public Parent {
 public:
  template <typename... Args>
  explicit RegisterIoAdapter(Args&&... args) : Parent(std::forward<Args>(args)...) {}

  template <typename T>
  void Write(T val, uint32_t offset) {
    if constexpr (sizeof(T) == sizeof(uint32_t)) {
      this->Write32(val, static_cast<uint32_t>(offset));
    } else if constexpr (sizeof(T) == sizeof(uint64_t)) {
      // Order of writes is arbitrary.
      this->Write32(static_cast<uint32_t>(val), offset);
      this->Write32(static_cast<uint32_t>(val >> 32), offset + 4);
    } else {
      static_assert(always_false<T>);
    }
  }
  template <typename T>
  T Read(uint32_t offset) {
    if constexpr (sizeof(T) == sizeof(uint32_t)) {
      return this->Read32(offset);
    } else if constexpr (sizeof(T) == sizeof(uint64_t)) {
      // Order of reads is arbitrary.
      uint64_t value_high = this->Read32(offset + 4);
      uint64_t value_low = this->Read32(offset);
      return (value_high << 32) | value_low;
    } else {
      static_assert(always_false<T>);
    }
  }

 private:
  template <typename T>
  static constexpr std::false_type always_false{};
};

using RegisterIo = RegisterIoAdapter<magma::RegisterIo>;

}  // namespace mali

#endif  // SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_MALI_REGISTER_IO_H_
