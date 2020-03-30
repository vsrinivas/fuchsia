// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_VMO_STORE_STORED_VMO_H_
#define SRC_LIB_VMO_STORE_STORED_VMO_H_

#include <lib/fzl/pinned-vmo.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <utility>

#include <fbl/alloc_checker.h>
#include <fbl/span.h>

namespace vmo_store {

namespace internal {
template <typename M>
struct MetaStorage {
  explicit MetaStorage(M&& value) : value(std::move(value)) {}
  M value;
};
template <>
struct MetaStorage<void> {};
}  // namespace internal

// A VMO stored in a `VmoStore`.
// A `StoredVmo` may have optional `Meta` user metadata associated with it.
template <typename Meta>
class StoredVmo {
 public:
  template <typename = std::enable_if<std::is_void<Meta>::value>>
  explicit StoredVmo(zx::vmo vmo) : vmo_(std::move(vmo)) {}

  template <typename M = Meta, typename = std::enable_if<!std::is_void<Meta>::value>>
  StoredVmo(zx::vmo vmo, M meta) : vmo_(std::move(vmo)), meta_(std::move(meta)) {}

  template <typename M = Meta, typename = std::enable_if<!std::is_void<Meta>::value>>
  M& meta() {
    return meta_.value;
  }

  // Maps the entire VMO to virtual memory with `options`.
  //
  // If `manager` is not provided, the root VMAR is used.
  //
  // Returns `ZX_ERR_ALREADY_BOUND` if the VMO is already mapped.
  // For other possible errors, see `fzl::VmoMapper::Map`.
  zx_status_t Map(zx_vm_option_t options, fbl::RefPtr<fzl::VmarManager> manager = nullptr) {
    if (mapper_.start()) {
      return ZX_ERR_ALREADY_BOUND;
    }
    return mapper_.Map(vmo_, 0, 0, options, std::move(manager));
  }

  // Pins the VMO using `bti`.
  //
  // `options` is one or more in the set ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS.
  // If `index` is true, enables fast indexing of regions to be fetched through `GetPinnedRegions`.
  //
  // Returns `ZX_ERR_ALREADY_BOUND` if the VMO is already pinned.
  // For other possible errors, see `fzl::PinnedVmo::Pin`.
  zx_status_t Pin(const zx::bti& bti, uint32_t options, bool index = true) {
    if (pinned_.region_count() != 0) {
      return ZX_ERR_ALREADY_BOUND;
    }
    zx_status_t status = pinned_.Pin(vmo_, bti, options);
    if (status != ZX_OK) {
      return status;
    }
    // Build a lookup table to be able to acquire arbitrary pinned VMO regions in O(logn) if more
    // than one region is pinned and indexing was requested.
    if (pinned_.region_count() == 1 || !index) {
      return ZX_OK;
    }
    fbl::AllocChecker ac;
    pinned_region_index_.reset(new (&ac) uint64_t[pinned_.region_count()]);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    uint64_t offset = 0;
    for (uint32_t i = 0; i < pinned_.region_count(); i++) {
      pinned_region_index_[i] = offset;
      offset += pinned_.region(i).size;
    }
    return ZX_OK;
  }

  // Accesses mapped VMO data.
  // An empty span is returned if the VMO was not mapped to virtual memory.
  fbl::Span<uint8_t> data() {
    return fbl::Span<uint8_t>(static_cast<uint8_t*>(mapper_.start()), mapper_.size());
  }

  // Get an unowned handle to the VMO.
  zx::unowned_vmo vmo() { return zx::unowned_vmo(vmo_); }
  // Accessor for pinned VMO regions.
  const fzl::PinnedVmo& pinned_vmo() { return pinned_; }

  // Gets the pinned regions from the VMO at `offset` with `len` bytes.
  //
  // `out_regions` is filled with `Region`s matching the provided range up to `region_count`
  // entries.
  // `region_count_actual` contains the number of regions in `out_regions` on success.
  //
  // Returns `ZX_ERR_BAD_STATE` if the VMO is not pinned, or region indexing was not enabled during
  // pinning.
  // Returns `ZX_ERR_OUT_RANGE` if the requested range does not fit within the pinned VMO.
  // Returns `ZX_ERR_BUFFER_TOO_SMALL` if all the necessary regions to cover the requested range
  // won't fit the provided buffer. In this case, `region_count_actual` contains the necessary
  // number of regions to fulfill the range and `out_regions` is filled up to `region_count`.
  //
  // Calling with `out_regions == nullptr` and `region_count = 0` is a valid pattern to query the
  // amount of regions required.
  //
  // Note that there are no alignment requirements on `offset` or `len`, the physical addresses kept
  // by the `VmoPinner` are just incremented by `offset`, callers must ensure alignment as
  // appropriate for the intended use of the pinned regions.
  zx_status_t GetPinnedRegions(uint64_t offset, uint64_t len, fzl::PinnedVmo::Region* out_regions,
                               size_t region_count, size_t* region_count_actual) {
    // Can't get regions if there aren't any or if indexing wasn't performed for more than 1
    // region.
    if (pinned_.region_count() == 0 || (pinned_.region_count() > 1 && !pinned_region_index_)) {
      *region_count_actual = 0;
      return ZX_ERR_BAD_STATE;
    }
    if (pinned_.region_count() == 1) {
      *region_count_actual = 1;
      auto& region = pinned_.region(0);
      if (offset + len > region.size) {
        return ZX_ERR_OUT_OF_RANGE;
      }
      if (region_count == 0) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      out_regions->phys_addr = region.phys_addr + offset;
      out_regions->size = len;
      return ZX_OK;
    }

    // If there's more than one region in the pinned VMO, binary search the offset start and start
    // filling out our output.
    *region_count_actual = 0;
    // Binary search the region where offset will start.
    auto* first = &pinned_region_index_[0];
    auto* last = &pinned_region_index_[pinned_.region_count()];
    // std::upper_bound will find the first iterator position that is larger than `offset` or `last`
    // if none is found.
    auto* upper = std::upper_bound(first, last, offset);
    // This shouldn't happen since we know the first position of the array always has offset 0, it
    // can't be greater than any unsigned offset.
    ZX_ASSERT(first != upper);
    upper--;
    ZX_ASSERT(*upper <= offset);
    uint64_t region_offset = offset - *upper;
    uint32_t region_index = (upper - first);
    // If the region offset is larger than the selected region's size, it means our offset is out of
    // range.
    if (region_offset >= pinned_.region(region_index).size) {
      // This should only happen if we landed at the last index.
      ZX_ASSERT(region_index == pinned_.region_count() - 1);
      return ZX_ERR_OUT_OF_RANGE;
    }
    auto* out_end = out_regions + region_count;
    while (len != 0 && region_index < pinned_.region_count()) {
      auto& region = pinned_.region(region_index);
      ZX_ASSERT(region.size > region_offset);
      uint64_t use_len = std::min(region.size - region_offset, len);

      if (out_regions != out_end) {
        out_regions->phys_addr = region.phys_addr + region_offset;
        out_regions->size = use_len;
        out_regions++;
      }

      (*region_count_actual)++;
      len -= use_len;
      // After the first region is evaluated, the region offset becomes zero. It's only needed for
      // the very first region.
      region_offset = 0;
      region_index++;
    }
    if (len != 0) {
      // Unable collect the entire length, meaning that the provided offset and length fall out of
      // bounds of our pinned VMO.
      return ZX_ERR_OUT_OF_RANGE;
    }
    // Return ZX_ERR_BUFFER_TOO_SMALL if we weren't able to write all regions to the output buffer.
    return *region_count_actual <= region_count ? ZX_OK : ZX_ERR_BUFFER_TOO_SMALL;
  }

  StoredVmo(StoredVmo&& other) noexcept = default;
  StoredVmo& operator=(StoredVmo&& other) noexcept = default;

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(StoredVmo);

 private:
  zx::vmo vmo_;
  internal::MetaStorage<Meta> meta_;
  fzl::VmoMapper mapper_;
  fzl::PinnedVmo pinned_;
  // pinned_region_index_ is allocated with `pinned_.region_count()` entries.
  std::unique_ptr<uint64_t[]> pinned_region_index_;
};

}  // namespace vmo_store

#endif  // SRC_LIB_VMO_STORE_STORED_VMO_H_
