// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_POOL_H_
#define ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_POOL_H_

#include <lib/fit/function.h>
#include <lib/fitx/result.h>
#include <lib/memalloc/range.h>
#include <lib/stdcompat/span.h>
#include <stdio.h>
#include <zircon/boot/image.h>

#include <array>
#include <string_view>

#include <fbl/intrusive_double_list.h>

namespace memalloc {

// Pool is a container that tracks ranges of physical memory and allocates
// available regions of RAM from among them.
//
// One initializes a Pool with a variable number of arrays of memory ranges.
// Except among extended types (see Type documentation), the ranges are
// permitted to overlap with one another to an arbitrary degree. In practice,
// the main array of ranges would be supplied by a ZBI or synthesized from a
// legacy booting protocol, while the others would consist of other auxiliary
// reserved areas known to be in use at the time of initialization
// (e.g., physboot's load image and the data ZBI itself). Despite arbitrarily
// overlapping inputs, Pool gives normalized outputs: iteration yields ranges
// that are lexicographically ordered, mutually disjoint, and for which
// addr + size does not overflow.
//
// Pool dynamically uses ranges of the free RAM it encodes for bookkeeping
// space. On initialization, it will attempt to find initial chunks to cover
// space to track the first crop of normalized ranges. With further allocation,
// fragmentation will increase and Pool will internally allocate more such
// space to manage it. Bookkeeping memory will also avoid the zero(th) page.
//
// [0, kNullPointerRegionEnd) is reserved from use by Pool: any kFreeRam
// subranges of this range passed to Init() are automatically converted to type
// kNullPointerRegion. Nothing will be allocated out of this region, including
// bookkeeping.
//
// Pool is neither copyable nor movable.
//
class Pool {
 private:
  // Forward-declared; defined below.
  struct NodeTraits;

  // The internal doubly-linked list type whose iterators - which are public
  // API - deference as const MemRange&.
  using List =
      fbl::DoublyLinkedList<MemRange*, fbl::DefaultObjectTag, fbl::SizeOrder::N, NodeTraits>;

 public:
  // Pool decouples the memory tracked by it from the dynamic memory it
  // actually uses (i.e., bookkkeeping) by way of a function of this type. A
  // BookkeepingAddressToPointer takes a would-be bookkeeping range
  // (start + size) and maps it to a corresponding region where the bookkeeping
  // data structures will actually be stored. While the actual bookkeeping
  // range will differ in general from the pre-translated range, Pool will only
  // track the latter (after blindly using the former); the onus is on the
  // caller to manage the translated ranges: There are two main uses for such a
  // feature:
  //
  // (*) testing (unit- and fuzz-): the decoupling is imperative for fully
  // testing Pool. Since bookkeeping space is carved out from among the input
  // RAM ranges, Pool would otherwise constrain the input RAM to always being
  // actually usable memory, which would make testing awkward limit and limit
  // observable behaviour in a virtualized environment (e.g., that relating to
  // the zero page). In this case, a function that translates bookkeeping space
  // to heap would be simple and effective.
  //
  // (*) encapsulating MMU-like management: input ranges of memory could
  // conceivably by inaccessible by default and require preparation by such a
  // function. For example, perhaps pages of free RAM would need to be mapped
  // before Pool could make use of them. Or, in a more specific case, take
  // 64-bit addressable hardware in 32-bit mode: input ranges of memory could
  // live in the upper 2^32 of the address space, but would be inaccessible
  // until mapped into the lower 2^32 values.
  //
  using BookkeepingAddressToPointer =
      fit::inline_function<std::byte*(uint64_t addr, uint64_t size)>;

  // The size of a chunk of free RAM reserved for internal Pool bookkeeping.
  // The value is ultimately arbitrary, but is chosen with the expectation that
  // it is sufficiently large to avoid fragmentation of the available memory in
  // the pool.
  static constexpr uint64_t kBookkeepingChunkSize = 0x1000;

  // The end of the kNullPointerRegion range specified above.
  static constexpr uint64_t kNullPointerRegionEnd = 0x10000;

  // Default-construction uses the identity mapping for a
  // BookkeepingAddressToPointer.
  Pool() = default;

  Pool(const Pool&) = delete;
  Pool(Pool&&) = delete;

  explicit Pool(BookkeepingAddressToPointer bookkeeping_pointer)
      : bookkeeping_pointer_(std::move(bookkeeping_pointer)) {}

  ~Pool() {
    ranges_.clear_unsafe();
    unused_.clear_unsafe();
  }

  Pool& operator=(const Pool&) = delete;
  Pool& operator=(Pool&&) = delete;

  // Initializes a Pool from a variable number of memory ranges.
  //
  // The provided ranges cannot feature overlap among different extended
  // types, or between an extended type with one of kReserved or kPeripheral;
  // otherwise, arbitrary overlap is permitted.
  //
  // fitx::failed is returned if there is insufficient free RAM to use for
  // Pool's initial bookkeeping.
  //
  template <size_t N>
  fitx::result<fitx::failed> Init(std::array<cpp20::span<MemRange>, N> ranges) {
    return Init(ranges, std::make_index_sequence<N>());
  }

  using iterator = typename List::const_iterator;
  using const_iterator = iterator;

  iterator begin() const { return ranges_.begin(); }
  iterator end() const { return ranges_.end(); }

  bool empty() const { return ranges_.is_empty(); }

  const MemRange& front() const {
    ZX_ASSERT(!empty());
    return *begin();
  }

  const MemRange& back() const {
    ZX_ASSERT(!empty());
    return *std::prev(end());
  }

  // Returns a pointer to the tracked, normalized range containing the
  // provided address, if one exists.
  const MemRange* GetContainingRange(uint64_t addr);

  // Attempts to allocate memory out of free RAM of the prescribed type, size,
  // and alignment, and with the given largest possible address. The provided
  // type must be an extended type.
  //
  // Any returned address is guaranteed to be nonzero.
  //
  // fitx::failed is returned if there is insufficient free RAM to track any
  // new ranges or if there is no free RAM that meets the given constraints.
  //
  fitx::result<fitx::failed, uint64_t> Allocate(
      Type type, uint64_t size, uint64_t alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__,
      uint64_t max_addr = std::numeric_limits<uintptr_t>::max());

  // Attempts to perform a "weak allocation" of the given range, wherein all
  // kFreeRam subranges are updated to `type`. The given range must be
  // comprised of tracked subranges of extended type, kFreeRam, or
  // kBookkeeping. `addr + size` cannot exceed UINT64_MAX.
  //
  // The utility of weak allocation lies in situations where there is a special
  // range that we ultimately want reserved for "something" later on, but it is
  // immaterial what occupies it in the meantime, so long as nothing is further
  // allocated from there. For example, when loading a fixed-address kernel
  // image, we would want to prevent page tables - which must persist
  // across the boot -from being allocated out of that load range.
  //
  // fitx::failed is returned if there is insufficient bookkeeping to track any
  // new ranges of memory.
  //
  fitx::result<fitx::failed> UpdateFreeRamSubranges(Type type, uint64_t addr, uint64_t size);

  // Attempts to free a subrange of a previously allocated range or one of
  // extended type that had previously been passed to Init(). This subrange is
  // updated to have type kFreeRam.
  //
  // Freeing a range already tracked as kFreeRam is a no-op.
  //
  // fitx::failed is returned if there is insufficient memory to track the new
  // (subdivided) ranges of memory that would result from freeing.
  //
  fitx::result<fitx::failed> Free(uint64_t addr, uint64_t size);

  // Pretty-prints the memory ranges contained in the pool.
  void PrintMemoryRanges(const char* prefix, FILE* f = stdout) const;

 private:
  using mutable_iterator = typename List::iterator;
  using TypeUpdateFunc = fit::inline_function<void(Type&)>;

  // Custom node struct defined so that List's iterators dereference instead as
  // const MemRange&.
  struct Node : public MemRange {
    using State = fbl::DoublyLinkedListNodeState<MemRange*, fbl::NodeOptions::AllowClearUnsafe>;

    State node_;
  };

  struct NodeTraits {
    using NodeState = typename Node::State;

    static NodeState& node_state(MemRange& element) { return static_cast<Node&>(element).node_; }
  };

  // Ultimately deferred to as the actual initialization routine.
  fitx::result<fitx::failed> Init(cpp20::span<internal::MemRangeIterationContext> state);

  template <size_t... I>
  fitx::result<fitx::failed> Init(std::array<cpp20::span<MemRange>, sizeof...(I)> ranges,
                                  std::index_sequence<I...> seq) {
    std::array state{internal::MemRangeIterationContext(ranges[I])...};
    return Init({state});
  }

  // Similar semantics to Allocate(), FindAllocatable() is its main allocation
  // subroutine: it only finds a suitable address to allocate (not actually
  // performing the allocation).
  fitx::result<fitx::failed, uint64_t> FindAllocatable(
      Type type, uint64_t size, uint64_t alignment,
      uint64_t max_addr = std::numeric_limits<uintptr_t>::max());

  // On success, returns disconnected node with the given range information;
  // fails if there is insufficient bookkeeping space for the new node.
  fitx::result<fitx::failed, Node*> NewNode(const MemRange& range);

  // Insert creates a new node for a range that is expected to already be a
  // subrange of an existing node. No policy checking is done to determine
  // whether inserting a subrange of type `range.type` is actually kosher;
  // fails if there is insufficient bookkeeping space for the new node. As an
  // optimization if known, the iterator pointing to the parent node may be
  // provided.
  fitx::result<fitx::failed, mutable_iterator> InsertSubrange(
      const MemRange& range, std::optional<mutable_iterator> parent_it = std::nullopt);

  // Merges into `it` any neighboring nodes with directly adjacent ranges of
  // the same type.
  mutable_iterator Coalesce(mutable_iterator it);

  // Returns an iterator pointing to the node whose range contains
  // [addr, addr + size), returning ranges_.end() if no such node exists.
  mutable_iterator GetContainingNode(uint64_t addr, uint64_t size);

  // Converts as much of [addr, addr + size) as bookkeeping memory as possible,
  // returning the address just after what it was able to convert.
  std::byte* PopulateAsBookkeeping(std::byte* addr, uint64_t size);

  // Attempts to ensure - through dynamic allocation of more space if need be -
  // that at least two unused bookkeeping nodes are available. N = 2 here is
  // the maximum number of new nodes other Pool methods might need in order to
  // successfully complete. This method is used in a best-effort context: if
  // there is insufficient space, the following operation will be permitted to
  // complete and report exactly that.
  void TryToEnsureTwoBookkeepingNodes();

  BookkeepingAddressToPointer bookkeeping_pointer_ = [](uint64_t addr, uint64_t size) {
    return reinterpret_cast<std::byte*>(addr);
  };

  // The list of unused nodes. We avoid the term "free" to disambiguate from
  // "free memory", which is unrelated to this list. The nodes stored within
  // should be treated as having garbage values for its fields; internal logic
  // must fully populate these fields (e.g., via NewNode()) when transferring
  // nodes to ranges_.
  List unused_;

  // The tracked, normalized ranges of memory. Normalization is an invariant:
  // at any time, the ranges within this list are lexicographically sorted,
  // mutually disjoint, maximally contiguous, and where addr + size does not
  // overflow.
  List ranges_;
};

}  // namespace memalloc

#endif  // ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_POOL_H_
