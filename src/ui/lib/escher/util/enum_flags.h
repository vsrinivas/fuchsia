// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_ENUM_FLAGS_H_
#define SRC_UI_LIB_ESCHER_UTIL_ENUM_FLAGS_H_

#include "src/ui/lib/escher/util/enum_utils.h"

namespace escher {

// Wrapper to allow bitwise operations on the members of an enum class.
// TODO(fxbug.dev/7239): write unit tests.
template <typename BitT>
class EnumFlags {
 public:
  using BitType = BitT;
  using MaskType = typename std::underlying_type<BitT>::type;

  EnumFlags() : mask_(0) {}
  EnumFlags(BitType bit) : mask_(static_cast<MaskType>(bit)) {}
  EnumFlags(const EnumFlags<BitType>& other) : mask_(other.mask_) {}
  explicit EnumFlags(MaskType flags) : mask_(flags) {}

  EnumFlags<BitType>& operator=(const EnumFlags<BitType>& other) {
    mask_ = other.mask_;
    return *this;
  }

  EnumFlags<BitType>& operator&=(const EnumFlags<BitType>& other) {
    mask_ &= other.mask_;
    return *this;
  }

  EnumFlags<BitType>& operator|=(const EnumFlags<BitType>& other) {
    mask_ |= other.mask_;
    return *this;
  }

  EnumFlags<BitType>& operator^=(const EnumFlags<BitType>& other) {
    mask_ ^= other.mask_;
    return *this;
  }

  EnumFlags<BitType> operator&(const EnumFlags<BitType>& other) const {
    EnumFlags<BitType> result(*this);
    result &= other;
    return result;
  }

  EnumFlags<BitType> operator|(const EnumFlags<BitType>& other) const {
    EnumFlags<BitType> result(*this);
    result |= other;
    return result;
  }

  EnumFlags<BitType> operator^(const EnumFlags<BitType>& other) const {
    EnumFlags<BitType> result(*this);
    result ^= other;
    return result;
  }

  EnumFlags<BitType> operator~() const {
    EnumFlags<BitType> result(*this);
    result.mask_ ^= EnumCast(BitType::kAllFlags);
    return result;
  }

  bool operator!() const { return mask_ == 0; }

  bool operator==(const EnumFlags<BitType>& other) const { return mask_ == other.mask_; }

  bool operator!=(const EnumFlags<BitType>& other) const { return mask_ != other.mask_; }

  explicit operator bool() const { return mask_ != 0; }
  explicit operator MaskType() const { return mask_; }

 private:
  MaskType mask_;
};

template <typename BitT>
EnumFlags<BitT> operator&(BitT bit, const EnumFlags<BitT>& flags) {
  return flags & bit;
}

template <typename BitT>
EnumFlags<BitT> operator|(BitT bit, const EnumFlags<BitT>& flags) {
  return flags | bit;
}

template <typename BitT>
EnumFlags<BitT> operator^(BitT bit, const EnumFlags<BitT>& flags) {
  return flags ^ bit;
}

// Reduce boilerplate by bundling two things that clients always want anyway:
// - a more convenient name: e.g. MyFlags instead of EnumFlags<MyFlagBits>
// - two inline functions for implicitly going from "Bits" to "Flags".
#define ESCHER_DECLARE_ENUM_FLAGS(FLAGS_NAME, BITS_NAME)                                          \
  using FLAGS_NAME = EnumFlags<BITS_NAME>;                                                        \
  inline FLAGS_NAME operator|(BITS_NAME bit1, BITS_NAME bit2) { return FLAGS_NAME(bit1) | bit2; } \
  inline FLAGS_NAME operator~(BITS_NAME bit) { return ~(FLAGS_NAME(bit)); }

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_ENUM_FLAGS_H_
