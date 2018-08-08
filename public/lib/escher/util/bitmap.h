// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_BITMAP_H_
#define LIB_ESCHER_UTIL_BITMAP_H_

#include <cstdint>
#include <vector>

#include "lib/fxl/logging.h"

namespace escher {

// Stores boolean values as a tightly-packed bitmap.
class Bitmap {
 public:
  // The caller is responsible for ensuring that |bits| outlives the Bitmap.
  Bitmap(uint32_t* bits, uint32_t bits_size)
      : bits_(bits), bits_size_(bits_size) {}

  // Get the value of a single bit.
  bool Get(uint32_t offset) const {
    uint32_t* bits = GetBits(offset);
    uint32_t shift = offset % 32;
    return *bits & (1 << shift);
  }

  // Set the value of a single bit to 1.
  void Set(uint32_t offset) {
    uint32_t* bits = GetBits(offset);
    uint32_t shift = offset % 32;
    *bits |= 1 << shift;
  }

  // Clear the value of a single bit to 0.
  void Clear(uint32_t offset) {
    uint32_t* bits = GetBits(offset);
    uint32_t shift = offset % 32;
    *bits &= ~(1 << shift);
  }

  // Clear all values in the bitmap to 0.
  void ClearAll() {
    for (uint32_t i = 0; i < bits_size_; ++i) {
      bits_[i] = 0;
    }
  }

  // Return the number of bits that can be held by the Bitmap.
  uint32_t GetSize() const { return bits_size_ * 32; }

 protected:
  // Used by subclasses to change the size of the bit storage.  It is up to
  // the caller to copy the old values into the new memory, if desired.
  void SetBitStorage(uint32_t* bits, uint32_t bits_size) {
    bits_ = bits;
    bits_size_ = bits_size;
  }

 private:
  // Get the 32 bits that contains the bit at |offset|.
  uint32_t* GetBits(uint32_t offset) {
    FXL_DCHECK(offset / 32 <= bits_size_);
    return bits_ + offset / 32;
  }
  uint32_t* GetBits(uint32_t offset) const {
    FXL_DCHECK(offset / 32 <= bits_size_);
    return bits_ + offset / 32;
  }

  uint32_t* bits_ = nullptr;
  uint32_t bits_size_ = 0;
};

// An implmentation of Bitmap that manages its own storage.
class BitmapWithStorage : public Bitmap {
 public:
  BitmapWithStorage() : Bitmap(nullptr, 0) {}

  // Set the bitmap to have enough storage to hold |bit_count| bits.
  void SetSize(uint32_t bit_count) {
    uint32_t size = bit_count / 32 + 1;
    storage_.resize(size);
    SetBitStorage(storage_.data(), size);
  }

 private:
  std::vector<uint32_t> storage_;
};

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_BITMAP_H_
