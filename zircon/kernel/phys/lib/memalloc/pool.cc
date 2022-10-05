// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/fit/result.h>
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

// Two hex `uint64_t`s, plus "[0x", ", 0x", and ")".
constexpr int kRangeColWidth = 2 * 16 + 3 + 4 + 1;

// A rough estimate: 4 digits, a decimal point, and a letter for a size.
constexpr int kSizeColWidth = 7;

}  // namespace

fit::result<fit::failed> Pool::Init(cpp20::span<internal::RangeIterationContext> state,
                                    uint64_t min_addr, uint64_t max_addr) {
  RangeStream ranges(state);

  const size_t scratch_size = FindNormalizedRangesScratchSize(ranges.size()) * sizeof(void*);

  // We want enough bookkeeping to fit our initial pages as well as the
  // scratch buffer we will need for FindNormalizedRanges().
  const uint64_t bookkeeping_size =
      *Align(ranges.size() * sizeof(Node) + scratch_size, kBookkeepingChunkSize);

  std::optional<uint64_t> bookkeeping_addr;
  auto find_bookkeeping = [min_addr, max_addr, bookkeeping_size,
                           &bookkeeping_addr](const Range& range) {
    ZX_DEBUG_ASSERT(range.type == Type::kFreeRam);

    const uint64_t start = std::max(range.addr, min_addr);
    const uint64_t end = std::min(range.end(), max_addr);

    std::optional<uint64_t> aligned_start = Align(start, kBookkeepingChunkSize);
    if (!aligned_start || *aligned_start >= end || end - *aligned_start < bookkeeping_size) {
      return true;
    }
    // Found our bookkeeping space.
    bookkeeping_addr = aligned_start;
    return false;
  };
  FindNormalizedRamRanges(ranges, find_bookkeeping);
  if (!bookkeeping_addr) {
    return fit::failed();
  }

  // Convert our bookkeeping space before actual use, and zero out the mapped
  // area to be able to recast as Nodes in the valid, initial fbl linked list
  // node state. `[0, bookkeeping.size() - scratch_size)` of that space will
  // then be turned into unused nodes. The tail of FindNormalizedRanges()
  // scratch space will be reclaimed after we are done with it.
  std::byte* bookkeeping_ptr = bookkeeping_pointer_(*bookkeeping_addr, bookkeeping_size);
  cpp20::span<void*> find_scratch = {
      reinterpret_cast<void**>(bookkeeping_ptr + bookkeeping_size - scratch_size),
      scratch_size / sizeof(void*),
  };
  const std::byte* bookkeeping_end = bookkeeping_ptr + bookkeeping_size;
  bookkeeping_ptr = PopulateAsBookkeeping(bookkeeping_ptr, bookkeeping_size - scratch_size);
  ZX_ASSERT(bookkeeping_ptr);

  ranges.reset();
  bool alloc_failure = false;
  auto process_range = [this, &alloc_failure](const Range& range) {
    // Amongst normalized ranges, reserved ranges are just 'holes'.
    if (range.type == Type::kReserved) {
      return true;
    }

    if (auto result = NewNode(range); result.is_error()) {
      alloc_failure = true;
      return false;
    } else {
      Node* node = std::move(result).value();
      AppendNode(node);
    }
    return true;
  };
  ZX_ASSERT_MSG(memalloc::FindNormalizedRanges(ranges, find_scratch, process_range).is_ok(),
                "Pool::Init(): bad input: the provided ranges feature overlap among different "
                "extended types, or an extended type with one of kReserved or kPeripheral");
  if (alloc_failure) {
    return fit::failed();
  }

  // Now reclaim the tail as node space.
  PopulateAsBookkeeping(bookkeeping_ptr, bookkeeping_end - bookkeeping_ptr);

  // Track the bookkeeping range, so it is not later allocated.
  const Range bookkeeping{
      .addr = *bookkeeping_addr,
      .size = bookkeeping_size,
      .type = Type::kPoolBookkeeping,
  };
  if (auto result = InsertSubrange(bookkeeping); result.is_error()) {
    return result.take_error();
  }

  default_min_addr_ = min_addr;
  default_max_addr_ = max_addr;
  return fit::ok();
}

fit::result<fit::failed, Pool::Node*> Pool::NewNode(const Range& range) {
  ZX_DEBUG_ASSERT(range.type != Type::kReserved);  // Not tracked, by policy.

  if (unused_.is_empty()) {
    return fit::failed();
  }
  Range* node = unused_.pop_back();
  ZX_DEBUG_ASSERT(node);
  node->addr = range.addr;
  node->size = range.size;
  node->type = range.type;
  return fit::ok(static_cast<Node*>(node));
}

const Range* Pool::GetContainingRange(uint64_t addr) {
  auto it = GetContainingNode(addr, 1);
  return it == ranges_.end() ? nullptr : &*it;
}

fit::result<fit::failed, uint64_t> Pool::Allocate(Type type, uint64_t size, uint64_t alignment,
                                                  std::optional<uint64_t> min_addr,
                                                  std::optional<uint64_t> max_addr) {
  ZX_ASSERT(size > 0);
  uint64_t upper_bound = max_addr.value_or(default_max_addr_);
  uint64_t lower_bound = min_addr.value_or(default_min_addr_);
  ZX_ASSERT(lower_bound <= upper_bound);

  TryToEnsureTwoBookkeepingNodes();

  uint64_t addr = 0;
  if (auto result = FindAllocatable(type, size, alignment, lower_bound, upper_bound);
      result.is_error()) {
    return result;
  } else {
    addr = std::move(result).value();
  }

  const Range allocated{.addr = addr, .size = size, .type = type};
  if (auto result = InsertSubrange(allocated); result.is_error()) {
    return result.take_error();
  } else {
    auto it = std::move(result).value();
    Coalesce(it);
  }
  return fit::ok(addr);
}

fit::result<fit::failed, uint64_t> Pool::FindAllocatable(Type type, uint64_t size,
                                                         uint64_t alignment, uint64_t min_addr,
                                                         uint64_t max_addr) {
  ZX_DEBUG_ASSERT(IsExtendedType(type));
  ZX_DEBUG_ASSERT(size > 0);
  ZX_DEBUG_ASSERT(min_addr <= max_addr);
  if (size - 1 > max_addr - min_addr) {
    return fit::failed();
  }

  // We use a simple first-fit approach, ultimately assuming that allocation
  // patterns will not create a lot of fragmentation.
  for (const Range& range : *this) {
    if (range.type != Type::kFreeRam || range.end() <= min_addr) {
      continue;
    }
    if (range.addr > max_addr) {
      break;
    }

    // If we have already aligned past UINT64_MAX or the prescribed maximum
    // address, then the same will be true with any subsequent ranges, so we
    // can short-circuit now.
    std::optional<uint64_t> aligned = Align(std::max(range.addr, min_addr), alignment);
    if (!aligned || *aligned > max_addr - size + 1) {
      break;
    }

    if (*aligned >= range.end() || range.end() - *aligned < size) {
      continue;
    }
    return fit::ok(*aligned);
  }

  return fit::failed();
}

fit::result<fit::failed> Pool::Free(uint64_t addr, uint64_t size) {
  ZX_ASSERT(kMax - addr >= size);

  if (size == 0) {
    // Nothing to do.
    return fit::ok();
  }

  auto it = GetContainingNode(addr, size);
  ZX_ASSERT_MSG(it != ranges_.end(), "Pool::Free(): provided address range is untracked");

  // Double-freeing is a no-op.
  if (it->type == Type::kFreeRam) {
    return fit::ok();
  }

  // Try to proactively ensure two bookkeeping nodes, which might be required
  // by InsertSubrange() below.
  TryToEnsureTwoBookkeepingNodes();

  ZX_ASSERT(it->type != Type::kPoolBookkeeping);
  ZX_ASSERT(IsExtendedType(it->type));
  const Range range{.addr = addr, .size = size, .type = Type::kFreeRam};
  if (auto status = InsertSubrange(range, it); status.is_error()) {
    return status.take_error();
  } else {
    it = std::move(status).value();
  }
  Coalesce(it);

  return fit::ok();
}

fit::result<fit::failed, uint64_t> Pool::Resize(const Range& original, uint64_t new_size,
                                                uint64_t min_alignment) {
  ZX_ASSERT(new_size > 0);
  ZX_ASSERT(IsExtendedType(original.type));
  ZX_ASSERT(cpp20::has_single_bit(min_alignment));
  ZX_ASSERT(original.addr % min_alignment == 0);

  auto it = GetContainingNode(original.addr, original.size);
  ZX_ASSERT_MSG(it != ranges_.end(), "`original` is not a subset of a tracked range");

  // Already appropriately sized; nothing to do.
  if (new_size == original.size) {
    return fit::ok(original.addr);
  }

  // Smaller size; need only to free the tail.
  if (new_size < original.size) {
    if (auto result = Free(original.addr + new_size, original.size - new_size); result.is_error()) {
      return fit::failed();
    }
    return fit::ok(original.addr);
  }

  //
  // The strategy here onward is to see whether we can find a resize candidate
  // that overlaps with `original`. If so, then we commit to that and directly
  // update the relevant iterators to reflect the post-resize state; if not,
  // then we can reallocate with knowledge that nothing could possibly be
  // allocated into original range's space, allowing us to delay freeing it
  // until the end without fear of poor memory utilization.
  //

  // Now we consider to what extent we have space off the end of `original` to
  // resize into. This is only kosher if `original` is the tail of its tracked
  // parent range (so that there aren't any separate, previously-coalesced
  // ranges in the way) and if there is an adjacent free RAM range present to
  // spill over into.
  auto next = std::next(it);
  uint64_t wiggle_room_end = original.end();
  if (next != ranges_.end() && original.end() == next->addr && next->type == Type::kFreeRam) {
    ZX_DEBUG_ASSERT(it->end() == original.end());
    wiggle_room_end = next->end();

    // Can extend in place.
    if (wiggle_room_end - original.addr >= new_size) {
      uint64_t next_spillover = new_size - original.size;
      if (next->size == next_spillover) {
        RemoveNodeAt(next);
      } else {
        next->addr += next_spillover;
        next->size -= next_spillover;
      }
      it->size += next_spillover;
      return fit::ok(original.addr);
    }
  }

  // At this point, we might have a little room in the next range to spill over
  // into, but any range overlapping with `original` would need to spill over
  // into the previous.
  uint64_t need = new_size - (wiggle_room_end - original.addr);
  auto prev = it == ranges_.begin() ? ranges_.end() : std::prev(it);
  if (prev != ranges_.end() && prev->end() == it->addr &&  // Adjacent.
      prev->type == Type::kFreeRam &&                      // Free RAM.
      prev->size >= need &&                                // Enough space (% alignment).
      it->addr == original.addr) {                         // No coalesced ranges in the way.

    // Can take the maximal, aligned address at least `need` bytes away from
    // the original range as a candidate for the new root, which will only work
    // if it still lies within the previous range and isn't far enough away
    // that we wouldn't have overlap with `original`.
    uint64_t new_addr = (prev->end() - need) & -(min_alignment - 1);
    if (new_addr >= prev->addr && original.addr - new_addr < new_size) {
      uint64_t prev_spillover = original.addr - new_addr;
      if (prev->size == prev_spillover) {
        RemoveNodeAt(prev);
      } else {
        prev->size -= prev_spillover;
      }
      it->addr -= prev_spillover;
      it->size += prev_spillover;

      // If the new end spills over into the next range, we must update the
      // bookkeeping there; if it falls short of the original end, then there
      // is nothing left to do but free the tail.
      if (uint64_t new_end = new_addr + new_size; new_end > original.end()) {
        ZX_DEBUG_ASSERT(next != ranges_.end());
        ZX_DEBUG_ASSERT(next->addr == original.end());
        ZX_DEBUG_ASSERT(next->type == Type::kFreeRam);

        uint64_t next_spillover = new_end - original.end();
        if (next->size == next_spillover) {
          RemoveNodeAt(next);
        } else {
          next->addr += next_spillover;
          next->size -= next_spillover;
        }
        it->size += next_spillover;
        ZX_DEBUG_ASSERT(it->size >= new_size);
      } else if (new_end < original.end()) {
        auto result = Free(new_end, original.end() - new_end);
        if (result.is_error()) {
          return fit::failed();
        }
      }
      return fit::ok(new_addr);
    }
  }

  // No option left but to allocate a replacement.
  uint64_t new_addr = 0;
  if (auto result = Allocate(original.type, new_size, min_alignment); result.is_error()) {
    return fit::failed();
  } else {
    new_addr = std::move(result).value();
  }
  if (auto result = Free(original.addr, original.size); result.is_error()) {
    return fit::failed();
  }
  return fit::ok(new_addr);
}

fit::result<fit::failed> Pool::UpdateFreeRamSubranges(Type type, uint64_t addr, uint64_t size) {
  ZX_ASSERT(IsExtendedType(type));
  ZX_ASSERT(kMax - addr >= size);

  if (size == 0) {
    // Nothing to do.
    return fit::ok();
  }

  // Try to proactively ensure two bookkeeping nodes, which might be required
  // by InsertSubrange() below.
  TryToEnsureTwoBookkeepingNodes();

  mutable_iterator it = ranges_.begin();
  while (it != ranges_.end() && addr + size > it->addr) {
    if (addr < it->end() && it->type == Type::kFreeRam) {
      uint64_t first = std::max(it->addr, addr);
      uint64_t last = std::min(it->end(), addr + size);
      const Range range{.addr = first, .size = last - first, .type = type};
      if (auto status = InsertSubrange(range, it); status.is_error()) {
        return status.take_error();
      } else {
        it = std::move(status).value();
      }
      it = Coalesce(it);
    }
    ++it;
  }
  return fit::ok();
}

fit::result<fit::failed, Pool::mutable_iterator> Pool::InsertSubrange(
    const Range& range, std::optional<mutable_iterator> parent_it) {
  auto it = parent_it.value_or(GetContainingNode(range.addr, range.size));
  ZX_DEBUG_ASSERT(it != ranges_.end());

  //     .------------.
  //     |  ////////  |
  //     '------------'
  //     <---range---->
  //     <----*it----->
  if (it->addr == range.addr && it->size == range.size) {
    it->type = range.type;
    return fit::ok(it);
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
    return fit::ok(InsertNodeAt(node, it));
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
    return fit::ok(InsertNodeAt(node, next));
  }

  //     .------------+------------.------------.
  //     |            |  ////////  |            |
  //     '------------+------------'------------'
  //                  <---range---->
  //     <-----------------*it------------------>
  ZX_DEBUG_ASSERT(it->addr < range.addr);
  ZX_DEBUG_ASSERT(range.end() < containing_end);
  it->size = range.addr - it->addr;
  InsertNodeAt(node, next);

  Range after = {
      .addr = range.end(),
      .size = containing_end - range.end(),
      .type = it->type,
  };
  if (auto result = NewNode(after); result.is_error()) {
    return result.take_error();
  } else {
    node = std::move(result).value();
    ZX_DEBUG_ASSERT(node);
    InsertNodeAt(node, next);
  }

  return fit::ok(std::next(it));
}

Pool::mutable_iterator Pool::GetContainingNode(uint64_t addr, uint64_t size) {
  ZX_DEBUG_ASSERT(size <= kMax - addr);

  // Despite the name, this function gives us the first range that is
  // lexicographically >= [addr, addr + size)
  auto next = std::lower_bound(ranges_.begin(), ranges_.end(), Range{.addr = addr, .size = size});
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
      unused_.push_back(RemoveNodeAt(prev));
    }
  }
  if (it != ranges_.end()) {
    auto next = std::next(it);
    if (next != ranges_.end() && next->type == it->type && it->end() == next->addr) {
      it->size += next->size;
      unused_.push_back(RemoveNodeAt(next));
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
  if (auto result = FindAllocatable(Type::kPoolBookkeeping, kBookkeepingChunkSize,
                                    kBookkeepingChunkSize, default_min_addr_, default_max_addr_);
      result.is_error()) {
    return;
  } else {
    addr = std::move(result).value();
  }

  std::byte* ptr = bookkeeping_pointer_(addr, kBookkeepingChunkSize);
  ZX_ASSERT(ptr);
  PopulateAsBookkeeping(ptr, kBookkeepingChunkSize);

  const Range bookkeeping = {
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

  memset(addr, 0, static_cast<size_t>(size));
  std::byte* end = addr + size;
  while (addr < end && end - addr >= static_cast<int>(sizeof(Node))) {
    unused_.push_back(reinterpret_cast<Range*>(addr));
    addr += sizeof(Node);
  }
  return addr;
}

void Pool::AppendNode(Node* node) {
  ++num_ranges_;
  ranges_.push_back(node);
}

Pool::mutable_iterator Pool::InsertNodeAt(Node* node, mutable_iterator it) {
  ++num_ranges_;
  return ranges_.insert(it, node);
}

Pool::Node* Pool::RemoveNodeAt(mutable_iterator it) {
  ZX_DEBUG_ASSERT(num_ranges_ > 0);
  --num_ranges_;
  return static_cast<Node*>(ranges_.erase(it));
}

void Pool::PrintMemoryRanges(const char* prefix, FILE* f) const {
  PrintMemoryRangeHeader(prefix, f);
  for (const memalloc::Range& range : *this) {
    PrintOneMemoryRange(range, prefix, f);
  }
}

void Pool::PrintMemoryRangeHeader(const char* prefix, FILE* f) {
  fprintf(f, "%s: | %-*s | %-*s | Type\n", prefix, kRangeColWidth, "Physical memory range",
          kSizeColWidth, "Size");
}

void Pool::PrintOneMemoryRange(const memalloc::Range& range, const char* prefix, FILE* f) {
  pretty::FormattedBytes size(static_cast<size_t>(range.size));
  std::string_view type = ToString(range.type);
  fprintf(f, "%s: | [0x%016" PRIx64 ", 0x%016" PRIx64 ") | %*s | %-.*s\n",  //
          prefix, range.addr, range.end(),                                  //
          kSizeColWidth, size.c_str(), static_cast<int>(type.size()), type.data());
}

}  // namespace memalloc
