// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BITMAP_RLE_BITMAP_H_
#define BITMAP_RLE_BITMAP_H_

#include <stddef.h>
#include <zircon/types.h>

#include <memory>

#include <bitmap/bitmap.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>

namespace bitmap {

// A run-length encoded bitmap.
template <typename T>
class RleBitmapBase final : public Bitmap<T> {
 public:
  // Elements of the bitmap list
  struct Element : public fbl::DoublyLinkedListable<std::unique_ptr<Element>> {
    // The start of this run of 1-bits.
    T bitoff;
    // The number of 1-bits in this run.
    T bitlen;

    // The (inclusive) start of this run of 1-bits.
    T start() const { return bitoff; }

    // The (exclusive) end of this run of 1-bits.
    T end() const { return bitoff + bitlen; }
  };

 private:
  using ElementPtr = std::unique_ptr<Element>;
  // Private forward-declaration to share the type between the iterator type
  // and the internal list.
  using ListType = fbl::DoublyLinkedList<ElementPtr>;

 public:
  using const_iterator = typename ListType::const_iterator;
  using FreeList = ListType;

  constexpr RleBitmapBase() : num_elems_(0), num_bits_(0) {}

  RleBitmapBase(RleBitmapBase&& rhs) noexcept = default;
  RleBitmapBase& operator=(RleBitmapBase&& rhs) noexcept = default;

  // Returns the current number of ranges.
  size_t num_ranges() const { return num_elems_; }

  // Returns the current number of bits.
  T num_bits() const { return num_bits_; }

  zx_status_t Find(bool is_set, T bitoff, T bitmax, T run_len, T* out) const override;

  // Returns true if all the bits in [*bitoff*, *bitmax*) are set.  Afterwards,
  // *first_unset* will be set to the lesser of bitmax and the index of the
  // first unset bit after *bitoff*.
  bool Get(T bitoff, T bitmax, T* first_unset) const override;
  using Bitmap<T>::Get;

  // Sets all bits in the range [*bitoff*, *bitmax*).  Only fails on allocation
  // error or if bitmax < bitoff.
  zx_status_t Set(T bitoff, T bitmax) override;

  // Sets all bits in the range [*bitoff*, *bitmax*).  Only fails if
  // *bitmax* < *bitoff* or if an allocation is needed and *free_list*
  // does not contain one.
  //
  // *free_list* is a list of usable allocations.  If an allocation is needed,
  // it will be drawn from it.  This function is guaranteed to need at most
  // one allocation.  If any nodes need to be deleted, they will be appended
  // to *free_list*.
  zx_status_t SetNoAlloc(T bitoff, T bitmax, FreeList* free_list);

  // Clears all bits in the range [*bitoff*, *bitmax*).  Only fails on allocation
  // error or if bitmax < bitoff.
  zx_status_t Clear(T bitoff, T bitmax) override;

  // Clear all bits in the range [*bitoff*, *bitmax*).  Only fails if
  // *bitmax* < *bitoff* or if an allocation is needed and *free_list*
  // does not contain one.
  //
  // *free_list* is a list of usable allocations.  If an allocation is needed,
  // it will be drawn from it.  This function is guaranteed to need at most
  // one allocation.  If any nodes need to be deleted, they will be appended
  // to *free_list*.
  zx_status_t ClearNoAlloc(T bitoff, T bitmax, FreeList* free_list);

  // Clear all bits in the bitmap.
  void ClearAll() override;

  // Iterate over the ranges in the bitmap.  Modifying the list while
  // iterating over it may yield undefined results.
  const_iterator cbegin() const { return elems_.cbegin(); }
  const_iterator begin() const { return elems_.cbegin(); }
  const_iterator end() const { return elems_.cend(); }
  const_iterator cend() const { return elems_.cend(); }

 private:
  static ElementPtr AllocateElement(RleBitmapBase::FreeList* free_list);
  static void ReleaseElement(RleBitmapBase::FreeList* free_list, ElementPtr&& elem);

  zx_status_t SetInternal(T bitoff, T bitmax, FreeList* free_list);
  zx_status_t ClearInternal(T bitoff, T bitmax, FreeList* free_list);

  // The ranges of the bitmap. Ranges are ordered by ascending |bitoff| value.
  // When no "Set" operation is in progress, there should not be any overlapping ranges.
  ListType elems_;

  // The number of ranges in elems_.
  size_t num_elems_;

  // The number of total bits in elems_; i.e. the sum of the bitlen field of all stored
  // Elements.
  T num_bits_;
};

// -- Implementation --

// Allocate a new bitmap element.  If *free_list* is null, allocate one using
// new.  If *free_list* is not null, take one from *free_list*.
template <typename T>
typename RleBitmapBase<T>::ElementPtr RleBitmapBase<T>::AllocateElement(
    RleBitmapBase::FreeList* free_list) {
  if (free_list)
    return free_list->pop_front();

  fbl::AllocChecker ac;
  ElementPtr new_elem(new (&ac) Element);
  if (!ac.check()) {
    return ElementPtr();
  }
  return new_elem;
}

// Release the element *elem*.  If *free_list* is null, release the element
// with delete.  If *free_list* is not null, append it to *free_list*.
template <typename T>
void RleBitmapBase<T>::ReleaseElement(RleBitmapBase::FreeList* free_list, ElementPtr&& elem) {
  if (free_list) {
    free_list->push_back(std::move(elem));
  }
}

template <typename T>
zx_status_t RleBitmapBase<T>::Find(bool is_set, T bitoff, T bitmax, T run_len, T* out) const {
  *out = bitmax;

  // Loop through all existing elems to try to find a |run_len| length range of |is_set| bits.
  // On each loop, |bitoff| is guaranteed to be either within the current elem, or in the range
  // of unset bits leading up to it.
  // Therefore, we can check whether |run_len| bits between |bitmax| and |bitoff| exist before
  // the start of the elem (for unset runs), or within the current elem (for set runs).
  for (const auto& elem : elems_) {
    if (bitoff >= elem.end()) {
      continue;
    }
    if (bitmax - bitoff < run_len) {
      return ZX_ERR_NO_RESOURCES;
    }

    T elem_min = std::max(bitoff, elem.bitoff);  // Minimum valid bit within elem.
    T elem_max = std::min(bitmax, elem.end());   // Maximum valid bit within elem.

    if (is_set && elem_max > elem_min && elem_max - elem_min >= run_len) {
      // This element contains at least |run_len| bits
      // which are between |bitoff| and |bitmax|.
      *out = elem_min;
      return ZX_OK;
    }

    if (!is_set && bitoff < elem.bitoff && elem.bitoff - bitoff >= run_len) {
      // There are at least |run_len| bits between |bitoff| and the beginning of this element.
      *out = bitoff;
      return ZX_OK;
    }

    if (bitmax < elem.end()) {
      // We have not found a valid run, and the specified range
      // does not extend past this element.
      return ZX_ERR_NO_RESOURCES;
    }

    // Update bitoff to the next value we want to check within the range.
    bitoff = elem.end();
  }

  if (!is_set && bitmax - bitoff >= run_len) {
    // We have not found an element with bits > bitoff, which means there is an infinite unset
    // range starting at bitoff.
    *out = bitoff;
    return ZX_OK;
  }

  return ZX_ERR_NO_RESOURCES;
}

template <typename T>
bool RleBitmapBase<T>::Get(T bitoff, T bitmax, T* first_unset) const {
  for (const auto& elem : elems_) {
    if (bitoff < elem.bitoff) {
      break;
    }
    if (bitoff < elem.bitoff + elem.bitlen) {
      bitoff = elem.bitoff + elem.bitlen;
      break;
    }
  }
  if (bitoff > bitmax) {
    bitoff = bitmax;
  }
  if (first_unset) {
    *first_unset = bitoff;
  }

  return bitoff == bitmax;
}

template <typename T>
void RleBitmapBase<T>::ClearAll() {
  elems_.clear();
  num_elems_ = 0;
  num_bits_ = 0;
}

template <typename T>
zx_status_t RleBitmapBase<T>::Set(T bitoff, T bitmax) {
  return SetInternal(bitoff, bitmax, nullptr);
}

template <typename T>
zx_status_t RleBitmapBase<T>::SetNoAlloc(T bitoff, T bitmax, FreeList* free_list) {
  if (free_list == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  return SetInternal(bitoff, bitmax, free_list);
}

template <typename T>
zx_status_t RleBitmapBase<T>::Clear(T bitoff, T bitmax) {
  return ClearInternal(bitoff, bitmax, nullptr);
}

template <typename T>
zx_status_t RleBitmapBase<T>::ClearNoAlloc(T bitoff, T bitmax, FreeList* free_list) {
  if (free_list == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  return ClearInternal(bitoff, bitmax, free_list);
}

template <typename T>
zx_status_t RleBitmapBase<T>::SetInternal(T bitoff, T bitmax, FreeList* free_list) {
  if (bitmax < bitoff) {
    return ZX_ERR_INVALID_ARGS;
  }

  const T bitlen = bitmax - bitoff;
  if (bitlen == 0) {
    return ZX_OK;
  }

  ElementPtr new_elem = AllocateElement(free_list);
  if (!new_elem) {
    return ZX_ERR_NO_MEMORY;
  }
  ++num_elems_;
  new_elem->bitoff = bitoff;
  new_elem->bitlen = bitlen;

  auto ends_after = elems_.find_if(
      [bitoff](const Element& elem) -> bool { return elem.bitoff + elem.bitlen >= bitoff; });

  // Insert the new element before the first node that ends at a point >=
  // when we begin.
  elems_.insert(ends_after, std::move(new_elem));
  num_bits_ += bitlen;

  // If ends_after was the end of the list, there is no merging to do.
  if (ends_after == elems_.end()) {
    return ZX_OK;
  }

  auto itr = ends_after;
  Element& elem = *--ends_after;

  if (elem.bitoff >= itr->bitoff) {
    // Our range either starts before or in the middle/end of *elem*.
    // Adjust it so it starts at the same place as *elem*, to allow
    // the merge logic to not consider this overlap case.
    elem.bitlen += elem.bitoff - itr->bitoff;
    num_bits_ += elem.bitoff - itr->bitoff;
    elem.bitoff = itr->bitoff;
  }

  // Walk forwards and remove/merge any overlaps
  T max = elem.bitoff + elem.bitlen;
  while (itr != elems_.end()) {
    if (itr->bitoff > max) {
      break;
    }

    max = std::max(max, itr->bitoff + itr->bitlen);
    num_bits_ += max - elem.bitoff - itr->bitlen - elem.bitlen;
    elem.bitlen = max - elem.bitoff;
    auto to_erase = itr;
    ++itr;
    ReleaseElement(free_list, elems_.erase(to_erase));
    --num_elems_;
  }

  return ZX_OK;
}

template <typename T>
zx_status_t RleBitmapBase<T>::ClearInternal(T bitoff, T bitmax, FreeList* free_list) {
  if (bitmax < bitoff) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (bitmax - bitoff == 0) {
    return ZX_OK;
  }

  auto itr = elems_.begin();
  while (itr != elems_.end()) {
    if (itr->bitoff + itr->bitlen < bitoff) {
      ++itr;
      continue;
    }
    if (bitmax < itr->bitoff) {
      break;
    }
    if (itr->bitoff < bitoff) {
      if (itr->bitoff + itr->bitlen <= bitmax) {
        // '*itr' contains 'bitoff'.
        num_bits_ -= (itr->bitlen - (bitoff - itr->bitoff));
        itr->bitlen = bitoff - itr->bitoff;
        ++itr;
        continue;
      }
      // '*itr' contains [bitoff, bitmax), and we need to split it.
      ElementPtr new_elem = AllocateElement(free_list);
      if (!new_elem) {
        return ZX_ERR_NO_MEMORY;
      }
      ++num_elems_;
      new_elem->bitoff = bitmax;
      new_elem->bitlen = itr->bitoff + itr->bitlen - bitmax;

      elems_.insert_after(itr, std::move(new_elem));
      itr->bitlen = bitoff - itr->bitoff;
      num_bits_ -= (bitmax - bitoff);
      break;
    }
    if (bitmax < itr->bitoff + itr->bitlen) {
      // 'elem' contains 'bitmax'
      num_bits_ -= (bitmax - itr->bitoff);
      itr->bitlen = itr->bitoff + itr->bitlen - bitmax;
      itr->bitoff = bitmax;
      break;
    }
    // [bitoff, bitmax) fully contains '*itr'.
    num_bits_ -= itr->bitlen;
    auto to_erase = itr++;
    ReleaseElement(free_list, elems_.erase(to_erase));
    --num_elems_;
  }

  return ZX_OK;
}

using RleBitmap = RleBitmapBase<size_t>;
using RleBitmapElement = RleBitmap::Element;

}  // namespace bitmap

#endif  // BITMAP_RLE_BITMAP_H_
