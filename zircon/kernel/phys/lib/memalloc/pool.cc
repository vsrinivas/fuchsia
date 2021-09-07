// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/fitx/result.h>
#include <lib/memalloc/pool.h>
#include <lib/stdcompat/bit.h>
#include <lib/stdcompat/span.h>
#include <stdarg.h>
#include <stdio.h>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>

#include <pretty/cpp/sizes.h>

#include "algorithm.h"

namespace memalloc {

using namespace std::string_view_literals;

namespace {

constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();

constexpr std::optional<uint64_t> Align(uint64_t addr, uint64_t alignment) {
  ZX_DEBUG_ASSERT(cpp20::has_single_bit(alignment));

  // If `addr + alignment - 1` overflows, then that means addr lies within
  // [2^64 - alignment + 1, 2^64), from which it should be clear that it is not
  // aligned and nor can it be.
  if (unlikely(addr > kMax - (alignment - 1))) {
    return {};
  }
  return (addr + alignment - 1) & ~(alignment - 1);
}

}  // namespace

fitx::result<fitx::failed> Pool::Init(cpp20::span<internal::MemRangeIterationContext> state) {
  MemRangeStream ranges(state);

  const size_t scratch_size = FindNormalizedRangesScratchSize(ranges.size()) * sizeof(void*);
  MemRange bookkeeping = {
      // To be set by `find_bookkeeping()`.
      .addr = 0,
      // We want enough bookkeeping to fit our initial pages as well as the
      // scratch buffer we will need for FindNormalizedRanges().
      .size = *Align(ranges.size() * sizeof(Node) + scratch_size, kBookkeepingChunkSize),
      .type = Type::kPoolBookkeeping,
  };
  auto find_bookkeeping = [&bookkeeping](const MemRange& range) {
    ZX_DEBUG_ASSERT(range.type == Type::kFreeRam);
    // Align past the null pointer region, as bookkeeping is not permitted to
    // be allocated from there.
    std::optional<uint64_t> aligned =
        Align(std::max(range.addr, kNullPointerRegionEnd), kBookkeepingChunkSize);
    if (!aligned || *aligned >= range.end() || range.end() - *aligned < bookkeeping.size) {
      return true;
    }
    // Found our bookkeeping space.
    bookkeeping.addr = *aligned;
    return false;
  };
  FindNormalizedRamRanges(ranges, find_bookkeeping);
  if (bookkeeping.addr == 0) {
    return fitx::failed();
  }

  // Convert our bookkeeping space before actual use, and zero out the mapped
  // area to be able to recast as Nodes in the valid, initial fbl linked list
  // node state. `[0, bookkeeping.size() - scratch_size)` of that space will
  // then be turned into unused nodes. The tail of FindNormalizedRanges()
  // scratch space will be reclaimed after we are done with it.
  std::byte* bookkeeping_ptr = bookkeeping_pointer_(bookkeeping.addr, bookkeeping.size);
  cpp20::span<void*> find_scratch = {
      reinterpret_cast<void**>(bookkeeping_ptr + bookkeeping.size - scratch_size),
      scratch_size / sizeof(void*),
  };
  const std::byte* bookkeeping_end = bookkeeping_ptr + bookkeeping.size;
  bookkeeping_ptr = PopulateAsBookkeeping(bookkeeping_ptr, bookkeeping.size - scratch_size);
  ZX_ASSERT(bookkeeping_ptr);

  ranges.reset();
  bool alloc_failure = false;
  auto process_range = [this, &alloc_failure](const MemRange& range) {
    if (auto result = NewNode(range); result.is_error()) {
      alloc_failure = true;
      return false;
    } else {
      Node* node = std::move(result).value();
      ranges_.push_back(node);
    }
    return true;
  };
  ZX_ASSERT_MSG(memalloc::FindNormalizedRanges(ranges, find_scratch, process_range).is_ok(),
                "Pool::Init(): bad input: the provided ranges feature overlap among different "
                "extended types, or an extended type with one of kReserved or kPeripheral");
  if (alloc_failure) {
    return fitx::failed();
  }

  // Now reclaim the tail as node space.
  PopulateAsBookkeeping(bookkeeping_ptr, bookkeeping_end - bookkeeping_ptr);

  // Track the bookkeeping range, so it is not later allocated.
  if (auto result = InsertSubrange(bookkeeping); result.is_error()) {
    return result.take_error();
  }

  // Per the documentation.
  return UpdateFreeRamSubranges(Type::kNullPointerRegion, 0, kNullPointerRegionEnd);
}

fitx::result<fitx::failed, Pool::Node*> Pool::NewNode(const MemRange& range) {
  if (unused_.is_empty()) {
    return fitx::failed();
  }
  MemRange* node = unused_.pop_back();
  ZX_DEBUG_ASSERT(node);
  node->addr = range.addr;
  node->size = range.size;
  node->type = range.type;
  return fitx::ok(static_cast<Node*>(node));
}

const MemRange* Pool::GetContainingRange(uint64_t addr) {
  auto it = GetContainingNode(addr, 1);
  return it == ranges_.end() ? nullptr : &*it;
}

fitx::result<fitx::failed, uint64_t> Pool::Allocate(Type type, uint64_t size, uint64_t alignment,
                                                    uint64_t max_addr) {
  // Try to proactively ensure two bookkeeping nodes, which might be required
  // by InsertSubrange() below.
  TryToEnsureTwoBookkeepingNodes();

  uint64_t addr = 0;
  if (auto result = FindAllocatable(type, size, alignment, max_addr); result.is_error()) {
    return result;
  } else {
    addr = std::move(result).value();
  }

  const MemRange allocated{.addr = addr, .size = size, .type = type};
  if (auto result = InsertSubrange(allocated); result.is_error()) {
    return result.take_error();
  } else {
    auto it = std::move(result).value();
    Coalesce(it);
  }
  return fitx::ok(addr);
}

fitx::result<fitx::failed, uint64_t> Pool::FindAllocatable(Type type, uint64_t size,
                                                           uint64_t alignment, uint64_t max_addr) {
  ZX_ASSERT(IsExtendedType(type));
  ZX_ASSERT(size > 0);
  if (size > max_addr) {
    return fitx::failed();
  }

  // We use a simple first-fit approach, ultimately assuming that allocation
  // patterns will not create a lot of fragmentation.
  for (const MemRange& range : *this) {
    if (range.type != Type::kFreeRam) {
      continue;
    }

    // If we have already aligned past UINT64_MAX or the prescribed maximum
    // address, then the same will be true with any subsequent ranges, so we
    // can short-circuit now.
    std::optional<uint64_t> aligned = Align(range.addr, alignment);
    if (!aligned || *aligned > max_addr - size) {
      break;
    }

    if (*aligned >= range.end() || range.end() - *aligned < size) {
      continue;
    }

    ZX_DEBUG_ASSERT(*aligned);
    return fitx::ok(*aligned);
  }

  return fitx::failed();
}

fitx::result<fitx::failed> Pool::Free(uint64_t addr, uint64_t size) {
  ZX_ASSERT(kMax - addr >= size);

  if (size == 0) {
    // Nothing to do.
    return fitx::ok();
  }

  auto it = GetContainingNode(addr, size);
  ZX_ASSERT_MSG(it != ranges_.end(), "Pool::Free(): provided address range is untracked");

  // Double-freeing is a no-op.
  if (it->type == Type::kFreeRam) {
    return fitx::ok();
  }

  // Try to proactively ensure two bookkeeping nodes, which might be required
  // by InsertSubrange() below.
  TryToEnsureTwoBookkeepingNodes();

  ZX_ASSERT(it->type != Type::kPoolBookkeeping);
  ZX_ASSERT(IsExtendedType(it->type));
  const MemRange range{.addr = addr, .size = size, .type = Type::kFreeRam};
  if (auto status = InsertSubrange(range, it); status.is_error()) {
    return status.take_error();
  } else {
    it = std::move(status).value();
  }
  Coalesce(it);

  return fitx::ok();
}

fitx::result<fitx::failed> Pool::UpdateFreeRamSubranges(Type type, uint64_t addr, uint64_t size) {
  ZX_ASSERT(IsExtendedType(type));
  ZX_ASSERT(kMax - addr >= size);

  if (size == 0) {
    // Nothing to do.
    return fitx::ok();
  }

  // Try to proactively ensure two bookkeeping nodes, which might be required
  // by InsertSubrange() below.
  TryToEnsureTwoBookkeepingNodes();

  mutable_iterator it = ranges_.begin();
  while (it != ranges_.end() && addr + size > it->addr) {
    if (addr < it->end() && it->type == Type::kFreeRam) {
      uint64_t first = std::max(it->addr, addr);
      uint64_t last = std::min(it->end(), addr + size);
      const MemRange range{.addr = first, .size = last - first, .type = type};
      if (auto status = InsertSubrange(range, it); status.is_error()) {
        return status.take_error();
      } else {
        it = std::move(status).value();
      }
      it = Coalesce(it);
    }
    ++it;
  }
  return fitx::ok();
}

fitx::result<fitx::failed, Pool::mutable_iterator> Pool::InsertSubrange(
    const MemRange& range, std::optional<mutable_iterator> parent_it) {
  auto it = parent_it.value_or(GetContainingNode(range.addr, range.size));
  ZX_DEBUG_ASSERT(it != ranges_.end());

  //     .------------.
  //     |  ////////  |
  //     '------------'
  //     <---range---->
  //     <----*it----->
  if (it->addr == range.addr && it->size == range.size) {
    it->type = range.type;
    return fitx::ok(it);
  }

  // We know now that we will need at least one new node for `range`.
  Node* node = nullptr;
  if (auto result = NewNode(range); result.is_error()) {
    return result.take_error();
  } else {
    node = std::move(result).value();
  }
  ZX_DEBUG_ASSERT(node);

  //     .------------+------------.
  //     |  ////////  |            |
  //     '------------+------------'
  //     <---range---->
  //     <----------*it------------>
  if (it->addr == range.addr) {
    ZX_DEBUG_ASSERT(range.size < it->size);
    it->addr += range.size;
    it->size -= range.size;
    return fitx::ok(ranges_.insert(it, node));
  }

  const uint64_t containing_end = it->end();
  auto next = std::next(it);

  //     .------------+------------.
  //     |            |  ////////  |
  //     '------------+------------'
  //                  <---range---->
  //     <-----------*it----------->
  if (range.end() == it->end()) {
    ZX_DEBUG_ASSERT(it->addr < range.addr);
    it->size -= range.size;
    return fitx::ok(ranges_.insert(next, node));
  }

  //     .------------+------------.------------.
  //     |            |  ////////  |            |
  //     '------------+------------'------------'
  //                  <---range---->
  //     <-----------------*it------------------>
  ZX_DEBUG_ASSERT(it->addr < range.addr);
  ZX_DEBUG_ASSERT(range.end() < containing_end);
  it->size = range.addr - it->addr;
  ranges_.insert(next, node);

  MemRange after = {
      .addr = range.end(),
      .size = containing_end - range.end(),
      .type = it->type,
  };
  if (auto result = NewNode(after); result.is_error()) {
    return result.take_error();
  } else {
    node = std::move(result).value();
    ZX_DEBUG_ASSERT(node);
    ranges_.insert(next, node);
  }

  return fitx::ok(std::next(it));
}

Pool::mutable_iterator Pool::GetContainingNode(uint64_t addr, uint64_t size) {
  ZX_DEBUG_ASSERT(size <= kMax - addr);

  // Despite the name, this function gives us the first range that is
  // lexicographically >= [addr, addr + size)
  auto next =
      std::lower_bound(ranges_.begin(), ranges_.end(), MemRange{.addr = addr, .size = size});
  uint64_t range_end = addr + size;
  if (next != ranges_.end() && addr >= next->addr) {
    return range_end <= next->end() ? next : ranges_.end();
  }
  // If the first range lexicographically >= [addr, addr + size) is
  // ranges_.begin() and we did not enter the previous branch, then addr + size
  // exceeds the right endpoint of ranges_.begin().
  if (next == ranges_.begin()) {
    return ranges_.end();
  }
  auto prev = std::prev(next);
  return (prev->addr <= addr && range_end <= prev->end()) ? prev : ranges_.end();
}

Pool::mutable_iterator Pool::Coalesce(mutable_iterator it) {
  if (it != ranges_.begin()) {
    auto prev = std::prev(it);
    if (prev->type == it->type && prev->end() == it->addr) {
      it->addr = prev->addr;
      it->size += prev->size;
      unused_.push_back(ranges_.erase(prev));
    }
  }
  if (it != ranges_.end()) {
    auto next = std::next(it);
    if (next != ranges_.end() && next->type == it->type && it->end() == next->addr) {
      it->size += next->size;
      unused_.push_back(ranges_.erase(next));
    }
  }
  return it;
}

void Pool::TryToEnsureTwoBookkeepingNodes() {
  // Instead of iterating through `unused_` to compute the size, we make the
  // following O(1) check instead.
  auto begin = unused_.begin();
  auto end = unused_.end();
  bool one_or_less = begin == end || std::next(begin) == end;
  if (!one_or_less) {
    return;
  }

  uint64_t addr = 0;
  if (auto result =
          FindAllocatable(Type::kPoolBookkeeping, kBookkeepingChunkSize, kBookkeepingChunkSize);
      result.is_error()) {
    return;
  } else {
    addr = std::move(result).value();
  }

  std::byte* ptr = bookkeeping_pointer_(addr, kBookkeepingChunkSize);
  ZX_ASSERT(ptr);
  PopulateAsBookkeeping(ptr, kBookkeepingChunkSize);

  const MemRange bookkeeping = {
      .addr = addr,
      .size = kBookkeepingChunkSize,
      .type = Type::kPoolBookkeeping,
  };
  // Don't bother to check for errors: we have already populated the new
  // bookkeeping chunk, so things should succeed; else, we are in a pathological
  // state and should fail hard.
  auto it = InsertSubrange(bookkeeping).value();
  Coalesce(it);
}

std::byte* Pool::PopulateAsBookkeeping(std::byte* addr, uint64_t size) {
  ZX_DEBUG_ASSERT(addr);
  ZX_DEBUG_ASSERT(size <= kMax - reinterpret_cast<uint64_t>(addr));

  memset(addr, 0, size);
  std::byte* end = addr + size;
  while (addr < end && end - addr >= static_cast<int>(sizeof(Node))) {
    unused_.push_back(reinterpret_cast<MemRange*>(addr));
    addr += sizeof(Node);
  }
  return addr;
}

void Pool::PrintMemoryRanges(const char* prefix, FILE* f) const {
  // Two hex `uint64_t`s, plus "[0x", ", 0x", and ")".
  constexpr int kRangeColWidth = 2 * 16 + 3 + 4 + 1;
  // A rough estimate: 4 digits, a decimal point, and a letter for a size.
  constexpr int kSizeColWidth = 7;

  fprintf(f, "%s: | %-*s | %-*s | Type\n", prefix, kRangeColWidth, "Physical memory range",
          kSizeColWidth, "Size");
  for (const memalloc::MemRange& range : *this) {
    pretty::FormattedBytes size(range.size);
    std::string_view type = ToString(range.type);
    fprintf(f, "%s: | [0x%016" PRIx64 ", 0x%016" PRIx64 ") | %*s | %-.*s\n",  //
            prefix, range.addr, range.end(),                                  //
            kSizeColWidth, size.c_str(), static_cast<int>(type.size()), type.data());
  }
}

}  // namespace memalloc
