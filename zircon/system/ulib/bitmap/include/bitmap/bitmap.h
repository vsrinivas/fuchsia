// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BITMAP_BITMAP_H_
#define BITMAP_BITMAP_H_

#include <stddef.h>
#include <zircon/types.h>

namespace bitmap {

// An abstract bitmap.
template <typename T = size_t>
class Bitmap {
 public:
  virtual ~Bitmap() = default;

  // Finds a run of |run_len| |is_set| bits, between |bitoff| and |bitmax|.
  // Sets |out| with the start of the run, or |bitmax| if it is
  // not found in the provided range.
  // If the run is not found, "ZX_ERR_NO_RESOURCES" is returned.
  virtual zx_status_t Find(bool is_set, T bitoff, T bitmax, T run_len, T* out) const = 0;

  // Returns true in the bit at bitoff is set.
  virtual bool GetOne(T bitoff) const { return Get(bitoff, bitoff + 1, nullptr); }

  // Returns true if all the bits in [*bitoff*, *bitmax*) are set. Afterwards,
  // *first_unset* will be set to the lesser of bitmax and the index of the
  // first unset bit after *bitoff*.
  virtual bool Get(T bitoff, T bitmax, T* first_unset) const = 0;
  bool Get(T bitoff, T bitmax) const { return Get(bitoff, bitmax, nullptr); }

  // Sets the bit at bitoff.  Only fails on allocation error.
  virtual zx_status_t SetOne(T bitoff) { return Set(bitoff, bitoff + 1); }

  // Sets all bits in the range [*bitoff*, *bitmax*).  Only fails on
  // allocation error or if bitmax < bitoff.
  virtual zx_status_t Set(T bitoff, T bitmax) = 0;

  // Clears the bit at bitoff.  Only fails on allocation error.
  virtual zx_status_t ClearOne(T bitoff) { return Clear(bitoff, bitoff + 1); }

  // Clears all bits in the range [*bitoff*, *bitmax*).  Only fails on
  // allocation error or if bitmax < bitoff.
  virtual zx_status_t Clear(T bitoff, T bitmax) = 0;

  // Clear all bits in the bitmap.
  virtual void ClearAll() = 0;
};

}  // namespace bitmap

#endif  // BITMAP_BITMAP_H_
