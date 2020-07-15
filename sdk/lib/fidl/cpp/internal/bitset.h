// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_BITSET_H_
#define LIB_FIDL_CPP_INTERNAL_BITSET_H_

#include <cstddef>
#include <cstdint>

namespace fidl {
namespace internal {

// BitSet used to track field presence in HLCPP tables.
template <size_t N>
class BitSet final {
  static const size_t RoundUpN = (N + 63ull) & ~63ull;
  static const size_t StorageSize = RoundUpN / 64ull;

  static inline size_t StorageIndex(size_t index) { return index / 64ull; }
  static inline uint64_t BitMask(size_t index) { return 1ull << (index % 64ull); }

  // Return the index of the most significant bit set or -1 if none are set.
  static inline int64_t MaxBitSet(uint64_t input) {
    // __builtin_clzll is undefined for an input of 0.
    if (input == 0) {
      return -1;
    }
    return 63ll - __builtin_clzll(input);
  }

 public:
  BitSet() noexcept = default;
  BitSet(const BitSet& other) noexcept = default;
  BitSet& operator=(const BitSet& other) noexcept = default;

  template <size_t Index>
  inline bool IsSet() const noexcept {
    static_assert(Index < N, "index exceeds BitSet size");
    return (storage_[StorageIndex(Index)] & BitMask(Index)) != 0;
  }

  template <size_t Index>
  inline void Set() noexcept {
    static_assert(Index < N, "index exceeds BitSet size");
    storage_[StorageIndex(Index)] |= BitMask(Index);
  }

  template <size_t Index>
  inline void Clear() noexcept {
    static_assert(Index < N, "index exceeds BitSet size");
    storage_[StorageIndex(Index)] &= ~BitMask(Index);
  }

  bool IsEmpty() const noexcept {
    uint64_t cum_union = 0ull;
    for (size_t i = 0; i < StorageSize; i++) {
      cum_union |= storage_[i];
    }
    return cum_union == 0ull;
  }

  int64_t MaxSetIndex() const noexcept {
    static_assert(StorageSize >= 1, "storage size expected to be at least 1");
    for (int64_t i = StorageSize - 1; i >= 1; i--) {
      uint64_t val = storage_[i];
      if (val != 0) {
        return 64ll * i + MaxBitSet(val);
      }
    }
    return MaxBitSet(storage_[0]);
  }

 private:
  uint64_t storage_[StorageSize] = {};
};

// Specialization to avoid compile errors due to 0-sized array.
template <>
class BitSet<0> {
 public:
  BitSet() noexcept = default;
  BitSet(const BitSet& other) noexcept = default;
  BitSet& operator=(const BitSet& other) noexcept = default;

  bool IsEmpty() const noexcept { return true; }

  int64_t MaxSetIndex() const noexcept { return -1; }
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_BITSET_H_
