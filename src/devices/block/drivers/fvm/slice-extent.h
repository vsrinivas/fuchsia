// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_FVM_SLICE_EXTENT_H_
#define SRC_DEVICES_BLOCK_DRIVERS_FVM_SLICE_EXTENT_H_

#include <stdint.h>

#include <memory>

#include <fbl/alloc_checker.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/vector.h>

#include "src/storage/fvm/format.h"

namespace fvm {

class SliceExtent : public fbl::WAVLTreeContainable<std::unique_ptr<SliceExtent>> {
 public:
  SliceExtent() = delete;
  explicit SliceExtent(uint64_t vslice_start) : vslice_start_(vslice_start) {}
  SliceExtent(const SliceExtent&) = delete;
  SliceExtent(SliceExtent&&) = delete;
  SliceExtent& operator=(const SliceExtent&) = delete;
  SliceExtent& operator=(SliceExtent&&) = delete;
  ~SliceExtent() = default;

  uint64_t GetKey() const { return vslice_start_; }

  // Vslice start (inclusive)
  uint64_t start() const { return vslice_start_; }

  // Vslice end (exclusive)
  uint64_t end() const { return vslice_start_ + pslices_.size(); }

  // Extent length
  uint64_t size() const { return end() - start(); }

  // Look up a pslice given a vslice
  bool find(uint64_t vslice, uint64_t* out_pslice) const {
    size_t offset = vslice - vslice_start_;
    if (offset >= pslices_.size()) {
      return false;
    }
    *out_pslice = at(vslice);
    return true;
  }

  uint64_t at(uint64_t vslice) const {
    ZX_ASSERT(vslice >= vslice_start_);
    uint64_t offset = vslice - vslice_start_;
    ZX_ASSERT(offset < pslices_.size());
    return pslices_[offset];
  }

  bool contains(uint64_t vslice) const {
    uint64_t pslice;
    return find(vslice, &pslice);
  }

  // Breaks the extent from:
  //   [start(), end())
  // Into:
  //   [start(), vslice] and [vslice + 1, end()).
  // Returns the latter extent on success; returns nullptr
  // if a memory allocation failure occurs.
  std::unique_ptr<SliceExtent> Split(uint64_t vslice);

  // Combines the other extent into this one.
  // 'other' must immediately follow the current slice.
  void Merge(const SliceExtent& other);

  void push_back(uint64_t pslice) { pslices_.push_back(pslice); }

  void pop_back() { pslices_.pop_back(); }

  bool empty() const { return pslices_.size() == 0; }

 private:
  friend class TypeWAVLTraits;

  fbl::Vector<uint64_t> pslices_;
  const uint64_t vslice_start_;
};

}  // namespace fvm

#endif  // SRC_DEVICES_BLOCK_DRIVERS_FVM_SLICE_EXTENT_H_
