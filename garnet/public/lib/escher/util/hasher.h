// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_HASHER_H_
#define LIB_ESCHER_UTIL_HASHER_H_

#include <cstdint>

#include "lib/escher/util/hash.h"
#include "lib/escher/util/hash_fnv_1a.h"
#include "lib/fxl/logging.h"

namespace escher {

// Hash is an "incremental hasher" which has convenient methods for hashing
// various data types.
class Hasher {
 public:
  explicit Hasher(uint64_t initial_hash = kHashFnv1OffsetBasis64)
      : value_(initial_hash) {}
  explicit Hasher(const Hash& hash) : value_(hash.val) {}

  // Return the current Hash value.
  Hash value() const {
    // Elsewhere in the code, it will be useful to use a hash-val of zero to
    // mean "lazily compute and return a hash value".
    FXL_DCHECK(value_ != 0);
    return {value_};
  }

  template <typename T, class Enable = void>
  inline void data(const T* data, size_t count) {
    static_assert(std::is_integral<T>::value, "data must be integral.");
    while (count--) {
      value_ = (value_ ^ *data) * kHashFnv1Prime64;
      ++data;
    }
  }

  // Treat the structure as if it were an array of uint32_t.  Caller must be
  // careful not to have any padding bits.  The current implementation is
  // limited to structure that are multiples of 4 bytes, because that's what
  // was needed at the time; this should be extended when necessary to handle
  // the remaining 1-3 bytes.
  //
  // NOTE: The name "struc" is short for "struct"; we'd use that, except that
  // it's a reserved word in C++.
  template <typename T>
  inline void struc(const T& t) {
    // Implementation detail.  Can relax at some point.
    static_assert(sizeof(T) % 4 == 0, "struct must be multiple of 4 bytes");

    int count = sizeof(T) / 4;
    const uint32_t* as_ints = reinterpret_cast<const uint32_t*>(&t);
    while (count--) {
      u32(*as_ints);
      ++as_ints;
    }
  }

  inline void u32(const uint32_t value) {
// TODO(SCN-646): This uses a modified FNV-1a hash.  Instead of operating on
// bytes, it operates on 4-byte chunks, resulting in a significant speedup.
// Not sure what this does to the hash quality; it doesn't appear to cause
// additional collisions. It's worth revisiting eventually.
#if 1
    value_ = (value_ ^ value) * kHashFnv1Prime64;
#else
    value_ = (value_ ^ (value & 0xff)) * kHashFnv1Prime64;
    value_ = (value_ ^ (value >> 8 & 0xff)) * kHashFnv1Prime64;
    value_ = (value_ ^ (value >> 16 & 0xff)) * kHashFnv1Prime64;
    value_ = (value_ ^ (value >> 24)) * kHashFnv1Prime64;
#endif
  }

  inline void i32(int32_t value) { u32(static_cast<uint32_t>(value)); }

  inline void f32(float value) {
    union {
      float f32;
      uint32_t u32;
    } u;
    u.f32 = value;
    u32(u.u32);
  }

  inline void u64(uint64_t value) {
    u32(value & 0xffffffffu);
    u32(value >> 32);
  }

  inline void i64(int64_t value) {
    i32(value & 0xffffffffu);
    i32(value >> 32);
  }

  inline void f64(double value) {
    union {
      double f64;
      uint64_t u64;
    } u;
    u.f64 = value;
    u64(u.u64);
  }

  inline void pointer(const void* ptr) {
    u64(reinterpret_cast<uintptr_t>(ptr));
  }

  inline void const_chars(const char* str) {
    // Ensure that even empty strings affect the hash, otherwise {"","","foo"}
    // would hash to the same as {"","foo",""}.
    u32(45);

    char c;
    while ((c = *str++) != '\0')
      u32(uint8_t(c));
  }

  inline void string(const std::string& str) {
    // Ensure that even empty strings affect the hash, otherwise {"","","foo"}
    // would hash to the same as {"","foo",""}.
    u32(45);

    data(str.data(), str.length());
  }

 private:
  uint64_t value_;
};

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_HASHER_H_
