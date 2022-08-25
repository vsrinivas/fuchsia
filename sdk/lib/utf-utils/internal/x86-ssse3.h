// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UTF_UTILS_INTERNAL_X86_SSSE3_H_
#define LIB_UTF_UTILS_INTERNAL_X86_SSSE3_H_

#ifdef __x86_64__

#include <lib/stdcompat/bit.h>
#include <x86intrin.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace utfutils {
namespace internal {
namespace x86 {

class Ssse3 {
 public:
  class Vector {
   public:
    using Underlying = __m128i;

    __attribute__((__target__("ssse3"))) static Vector LoadFromArray(const void *ptr) {
      return Vector(_mm_loadu_si128(static_cast<const Underlying *>(ptr)));
    }

    __attribute__((__target__("ssse3"))) static Vector Fill(uint8_t val) { return Vector(val); }

    __attribute__((__target__("ssse3"))) static Vector Set32(const std::array<uint8_t, 32> &vals) {
      return Vector(
          _mm_setr_epi8(cpp20::bit_cast<int8_t>(vals[16]), cpp20::bit_cast<int8_t>(vals[17]),
                        cpp20::bit_cast<int8_t>(vals[18]), cpp20::bit_cast<int8_t>(vals[19]),
                        cpp20::bit_cast<int8_t>(vals[20]), cpp20::bit_cast<int8_t>(vals[21]),
                        cpp20::bit_cast<int8_t>(vals[22]), cpp20::bit_cast<int8_t>(vals[23]),
                        cpp20::bit_cast<int8_t>(vals[24]), cpp20::bit_cast<int8_t>(vals[25]),
                        cpp20::bit_cast<int8_t>(vals[26]), cpp20::bit_cast<int8_t>(vals[27]),
                        cpp20::bit_cast<int8_t>(vals[28]), cpp20::bit_cast<int8_t>(vals[29]),
                        cpp20::bit_cast<int8_t>(vals[30]), cpp20::bit_cast<int8_t>(vals[31])));
    }

    __attribute__((__target__("ssse3"))) static Vector SetRepeat16(
        const std::array<uint8_t, 16> &vals) {
      return Vector(
          _mm_setr_epi8(cpp20::bit_cast<int8_t>(vals[0]), cpp20::bit_cast<int8_t>(vals[1]),
                        cpp20::bit_cast<int8_t>(vals[2]), cpp20::bit_cast<int8_t>(vals[3]),
                        cpp20::bit_cast<int8_t>(vals[4]), cpp20::bit_cast<int8_t>(vals[5]),
                        cpp20::bit_cast<int8_t>(vals[6]), cpp20::bit_cast<int8_t>(vals[7]),
                        cpp20::bit_cast<int8_t>(vals[8]), cpp20::bit_cast<int8_t>(vals[9]),
                        cpp20::bit_cast<int8_t>(vals[10]), cpp20::bit_cast<int8_t>(vals[11]),
                        cpp20::bit_cast<int8_t>(vals[12]), cpp20::bit_cast<int8_t>(vals[13]),
                        cpp20::bit_cast<int8_t>(vals[14]), cpp20::bit_cast<int8_t>(vals[15])));
    }

    Vector() = default;

    __attribute__((__target__("ssse3"))) explicit Vector(Underlying vec) : vec_(vec) {}

    __attribute__((__target__("ssse3"))) explicit Vector(uint8_t val)
        : vec_(_mm_set1_epi8(cpp20::bit_cast<int8_t>(val))) {}

    Vector(const Vector &) = default;
    Vector &operator=(const Vector &) = default;

    // NOLINTNEXTLINE(google-explicit-constructor)
    __attribute__((__target__("ssse3"))) operator Underlying() const { return vec_; }

    __attribute__((__target__("ssse3"))) const Underlying &operator*() const { return value(); }

    __attribute__((__target__("ssse3"))) Underlying operator*() { return value(); }

    __attribute__((__target__("ssse3"))) friend Vector operator|(const Vector &a, const Vector &b) {
      return Vector(_mm_or_si128(*a, *b));
    }

    __attribute__((__target__("ssse3"))) Vector &operator|=(const Vector &other) {
      *this = *this | other;
      return *this;
    }

    __attribute__((__target__("ssse3"))) friend Vector operator&(const Vector &a, const Vector &b) {
      return Vector(_mm_and_si128(*a, *b));
    }

    __attribute__((__target__("ssse3"))) friend Vector operator^(const Vector &a, const Vector &b) {
      return Vector(_mm_xor_si128(*a, *b));
    }

    __attribute__((__target__("ssse3"))) bool IsAllZero() const {
      return _mm_testz_si128(*this, *this) != 0;
    }

    __attribute__((__target__("ssse3"))) bool IsAscii() const {
      return _mm_movemask_epi8(*this) == 0;
    }

    __attribute__((__target__("ssse3"))) Underlying &value() { return vec_; }

    __attribute__((__target__("ssse3"))) const Underlying &value() const { return vec_; }

    __attribute__((__target__("ssse3"))) Vector SignedGt(const Vector &other) const {
      return Vector(_mm_cmpgt_epi8(*this, *other));
    }

    __attribute__((__target__("ssse3"))) Vector SaturatingSub(const Vector &subtrahend) const {
      return Vector(_mm_subs_epu8(*this, *subtrahend));
    }

    __attribute__((__target__("ssse3"))) Vector Shr4() const {
      // Shift in 16-bit mode and then mask off the top bits leftover.
      // Example: 0xABCD -> 0x0ABC -> 0x0A0C
      return Vector(_mm_srli_epi16(*this, 4)) & Vector::Fill(0x0F);
    }

    template <size_t N>
    __attribute__((__target__("ssse3"))) Vector Prev(const Vector &prev) const {
      static_assert(N <= 16, "Previous shift must be <= 16");

      // NOLINTNEXTLINE(google-readability-casting): clang-tidy mistakes this as a C-style cast.
      return Vector(_mm_alignr_epi8(*this, *prev, size_t{16} - N));
    }

    __attribute__((__target__("ssse3"))) Vector Lookup16(
        const std::array<uint8_t, 16> &table) const {
      return Vector(_mm_shuffle_epi8(Vector::SetRepeat16(table), *this));
    }

    __attribute__((__target__("ssse3"))) void StoreToArray(void *ptr) const {
      _mm_storeu_si128(static_cast<Underlying *>(ptr), *this);
    }

   private:
    Underlying vec_;
  };

  static_assert(sizeof(Vector) == sizeof(Vector::Underlying),
                "Vector and underlying type must be the same size");

  static void Prefetch(const void *ptr) { _mm_prefetch(ptr, _MM_HINT_T0); }

  static constexpr size_t VectorSize() { return sizeof(Vector); }

  static Vector Check2Or3Continuation(const Vector &prev2, const Vector &prev3) {
    Vector is_third_byte = prev2.SaturatingSub(Vector::Fill(0b11011111));
    Vector is_fourth_byte = prev3.SaturatingSub(Vector::Fill(0b11101111));

    return (is_third_byte | is_fourth_byte).SignedGt(Vector::Fill(0));
  }
};

}  // namespace x86
}  // namespace internal
}  // namespace utfutils

#endif

#endif  // LIB_UTF_UTILS_INTERNAL_X86_SSSE3_H_
