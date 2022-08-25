// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UTF_UTILS_INTERNAL_ARM_NEON_H_
#define LIB_UTF_UTILS_INTERNAL_ARM_NEON_H_

#ifdef __aarch64__

#include <arm_neon.h>
#include <lib/stdcompat/bit.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace utfutils {
namespace internal {
namespace arm {

class Neon {
 public:
  class Vector {
   public:
    using Underlying = uint8x16_t;

    static Vector LoadFromArray(const void *ptr) {
      return Vector(vld1q_u8(static_cast<const uint8_t *>(ptr)));
    }

    static Vector Fill(uint8_t val) { return Vector(val); }

    static Vector Set32(const std::array<uint8_t, 32> &vals) { return Vector(vld1q_u8(&vals[16])); }

    static Vector SetRepeat16(const std::array<uint8_t, 16> &vals) {
      return Vector(vld1q_u8(vals.data()));
    }

    Vector() = default;

    explicit Vector(Underlying vec) : vec_(vec) {}

    explicit Vector(uint8_t val) : vec_(vdupq_n_u8(val)) {}

    Vector(const Vector &) = default;
    Vector &operator=(const Vector &) = default;

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator Underlying() const { return vec_; }

    const Underlying &operator*() const { return value(); }

    Underlying operator*() { return value(); }

    friend Vector operator|(const Vector &a, const Vector &b) { return Vector(vorrq_u8(*a, *b)); }

    Vector &operator|=(const Vector &other) {
      *this = *this | other;
      return *this;
    }

    friend Vector operator&(const Vector &a, const Vector &b) { return Vector(vandq_u8(*a, *b)); }

    friend Vector operator^(const Vector &a, const Vector &b) { return Vector(veorq_u8(*a, *b)); }

    bool IsAllZero() const { return vmaxvq_u8(*this) == 0; }

    bool IsAscii() const { return vmaxvq_u8(*this) < 0b10000000; }

    Underlying &value() { return vec_; }

    const Underlying &value() const { return vec_; }

    Vector UnsignedGt(const Vector &other) const { return Vector(vcgtq_u8(*this, *other)); }

    Vector SaturatingSub(const Vector &subtrahend) const {
      return Vector(vqsubq_u8(*this, *subtrahend));
    }

    Vector Shr4() const { return Vector(vshrq_n_u8(*this, 4)); }

    template <size_t N>
    Vector Prev(const Vector &prev) const {
      static_assert(N <= 16, "Previous shift must be <= 16");

      return Vector(vextq_u8(*prev, *this, static_cast<int>(size_t{16} - N)));
    }

    Vector Lookup16(const std::array<uint8_t, 16> &table) const {
      return Vector(vqtbl1q_u8(Vector::SetRepeat16(table), *this));
    }

    void StoreToArray(void *ptr) const { vst1q_u8(static_cast<uint8_t *>(ptr), *this); }

   private:
    Underlying vec_;
  };

  static_assert(sizeof(Vector) == sizeof(Vector::Underlying),
                "Vector and underlying type must be the same size");

  static void Prefetch(const char *ptr) {}

  static constexpr size_t VectorSize() { return sizeof(Vector); }

  static Vector Check2Or3Continuation(const Vector &prev2, const Vector &prev3) {
    Vector is_third_byte = prev2.UnsignedGt(Vector::Fill(0b11011111));
    Vector is_fourth_byte = prev3.UnsignedGt(Vector::Fill(0b11101111));

    return is_third_byte | is_fourth_byte;
  }
};

}  // namespace arm
}  // namespace internal
}  // namespace utfutils

#endif

#endif  // LIB_UTF_UTILS_INTERNAL_ARM_NEON_H_
