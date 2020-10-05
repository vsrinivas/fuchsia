// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//
// Derived from zircon vm_aspace.

#include "simple_allocator.h"

#include <memory>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"

namespace magma {

// Returns true if the gap is good and addr_out is set.
// Otherwise false is returned and continue_search should be checked.
bool SimpleAllocator::CheckGap(SimpleAllocator::Region* prev, SimpleAllocator::Region* next,
                               uint64_t align, size_t size, uint64_t* addr_out,
                               bool* continue_search_out) {
  DASSERT(addr_out);
  DASSERT(continue_search_out);

  uint64_t gap_begin = prev ? (prev->base + prev->size) : base();
  uint64_t gap_end;  // last byte of a gap

  if (next) {
    if (gap_begin == next->base) {
      *continue_search_out = true;
      return false;
    }
    gap_end = next->base - 1;
  } else {
    if (gap_begin == base() + this->size()) {
      *continue_search_out = false;
      return false;
    }
    gap_end = base() + this->size() - 1;
  }

  *addr_out = magma::round_up(gap_begin, align);

  if (*addr_out < gap_begin) {
    *continue_search_out = false;
    return false;
  }

  if (*addr_out < gap_end && ((gap_end - *addr_out + 1) >= size))
    return true;

  *continue_search_out = true;
  return false;
}

//////////////////////////////////////////////////////////////////////////////

SimpleAllocator::Region::Region(uint64_t base_in, size_t size_in) : base(base_in), size(size_in) {
  DASSERT(size > 0);
  DASSERT(base + size - 1 >= base);
}

std::unique_ptr<SimpleAllocator> SimpleAllocator::Create(uint64_t base, size_t size) {
  return std::unique_ptr<SimpleAllocator>(new SimpleAllocator(base, size));
}

SimpleAllocator::SimpleAllocator(uint64_t base, size_t size) : AddressSpaceAllocator(base, size) {}

bool SimpleAllocator::Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out) {
  DLOG("Alloc size 0x%zx align_pow2 0x%x", size, align_pow2);
  DASSERT(addr_out);

  size = magma::round_up(size, magma::page_size());
  if (size == 0)
    return DRETF(false, "can't allocate size zero");

  DASSERT(magma::is_page_aligned(size));

  const auto page_shift = magma::page_shift();

  uint64_t align = 1UL << ((align_pow2 < page_shift) ? page_shift : align_pow2);
  uint64_t addr;
  bool continue_search;

  // try to pick spot at the beginning of address space
  if (CheckGap(nullptr, regions_.empty() ? nullptr : &regions_.front(), align, size, &addr,
               &continue_search)) {
    *addr_out = addr;
    regions_.emplace_front(addr, size);
    DLOG("allocated addr 0x%lx", addr);
    return true;
  }

  // search the middle of the list
  for (auto iter = regions_.begin(); continue_search && iter != regions_.end();) {
    auto prev = &(*iter);
    auto next = (++iter == regions_.end()) ? nullptr : &(*iter);
    if (CheckGap(prev, next, align, size, &addr, &continue_search)) {
      *addr_out = addr;
      regions_.insert(iter, Region(addr, size));
      DLOG("allocated addr 0x%lx", addr);
      return true;
    }
  }

  return DRETF(false, "failed to alloc");
}

bool SimpleAllocator::Free(uint64_t addr) {
  DLOG("Free addr 0x%lx", addr);

  auto iter = FindRegion(addr);
  if (iter == regions_.end())
    return DRETF(false, "couldn't find region to free");

  regions_.erase(iter);

  return true;
}

bool SimpleAllocator::GetSize(uint64_t addr, size_t* size_out) {
  auto iter = FindRegion(addr);
  if (iter == regions_.end())
    return DRETF(false, "couldn't find region");

  *size_out = iter->size;
  return true;
}

std::list<SimpleAllocator::Region>::iterator SimpleAllocator::FindRegion(uint64_t addr) {
  for (auto iter = regions_.begin(); iter != regions_.end(); iter++) {
    auto region = *iter;
    if ((addr >= region.base) && (addr <= region.base + region.size - 1))
      return iter;
  }

  return regions_.end();
}

}  // namespace magma
