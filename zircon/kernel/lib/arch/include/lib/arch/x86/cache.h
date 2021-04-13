// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_CACHE_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_CACHE_H_

#include <lib/arch/x86/cpuid.h>
#include <lib/stdcompat/bit.h>

#include <optional>
#include <type_traits>

namespace arch {

// Represents a single cache.
struct CpuCacheLevelInfo {
  size_t level;
  X86CacheType type;

  // The size, in KiB, of the cache available to each processor. In the case of
  // the last-level cache, however, this field might report the aggregate size
  // of all such caches on the package.
  size_t size_kb;

  // The number of sets in the cache available to each processor. In the case
  // of the last-level cache, however, this field might report the aggregate
  // number of sets across all such caches in the package.
  size_t number_of_sets;                  // Indeterminate if zero.
  size_t ways_of_associativity;           // Indeterminate if zero.
  std::optional<bool> fully_associative;  // Indeterminate if std::nullopt.

  // The number of bits to shift an APIC ID to get the associated "share ID":
  // processors with coinciding share IDs share this cache. If std::nullopt,
  // then it is indeterminate what the cache's shift is.
  std::optional<size_t> share_id_shift;
};

// Gives information on the set of caches in a package.
class CpuCacheInfo {
 public:
  using iterator = const CpuCacheLevelInfo*;
  using const_iterator = iterator;

  template <typename CpuidIoProvider,
            // To avoid precedence over copy and move constructors.
            typename = std::enable_if_t<!std::is_same_v<CpuidIoProvider, CpuCacheInfo>>>
  explicit CpuCacheInfo(CpuidIoProvider&& io) {
    // We first try the Intel v2 leaves - and then the AMD v2 leaves.
    // Hypervisors on AMD hosts might lay CPUID values in the Intel style - and
    // there is no harm in doing this in general as AMD hardware will tend to
    // reserve these Intel leaves as zero.

    if (TryV2Topology<CpuidIntelCacheTopologyA, CpuidIntelCacheTopologyB, CpuidIntelCacheTopologyC,
                      CpuidMaximumLeaf>(io)) {
      return;
    }

    if (TryV2Topology<CpuidAmdCacheTopologyA, CpuidAmdCacheTopologyB, CpuidAmdCacheTopologyC,
                      CpuidMaximumExtendedLeaf>(io)) {
      return;
    }

    // The extended leaves explicitly enumerate information about L1d, L1i, L2,
    // and L3, which was the original means of figuring out cache topology on
    // AMD.
    if (CpuidSupports<CpuidL3CacheInformation>(io)) {
      const auto l1d = io.template Read<CpuidL1DataCacheInformation>();
      const auto l1i = io.template Read<CpuidL1InstructionCacheInformation>();
      const auto l2 = io.template Read<CpuidL2CacheInformation>();
      const auto l3 = io.template Read<CpuidL3CacheInformation>();

      caches_[0] = {
          .level = 1,
          .type = X86CacheType::kData,
          .size_kb = l1d.size_kb(),
          .ways_of_associativity = l1d.ways_of_associativity(),
          .fully_associative = l1d.fully_associative(),
      };
      caches_[1] = {
          .level = 1,
          .type = X86CacheType::kInstruction,
          .size_kb = l1i.size_kb(),
          .ways_of_associativity = l1i.ways_of_associativity(),
          .fully_associative = l1i.fully_associative(),
      };
      caches_[2] = {
          .level = 2,
          .type = X86CacheType::kUnified,
          .size_kb = l2.size_kb(),
          .ways_of_associativity = l2.ways_of_associativity(),
          .fully_associative = l2.fully_associative(),
      };
      size_ = 3;

      if (l3.size()) {
        caches_[3] = {
            .level = 3,
            .type = X86CacheType::kUnified,
            // [amd/vol3]: E.4.5  Function 8000_0006h—L2 Cache and TLB and L3 Cache Information.
            //
            // `l3.size()` actually provides bounds for the total size of L3
            // cache across the package, in terms of 512 KiB blocks:
            // l3.size() * 512 ≤ total size KiB < (l3.size() + 1) * 512
            // In practice, the total size is a multiple of 512 and this
            // reports the actual total size.
            .size_kb = 512 * l3.size(),
            .ways_of_associativity = l3.ways_of_associativity(),
            .fully_associative = l3.fully_associative(),
        };
        size_ = 4;
      }
    }
  }

  CpuCacheInfo() = delete;

  iterator begin() const { return caches_; }

  iterator end() const { return caches_ + size_; }

  size_t size() const { return size_; }

  bool empty() const { return size_ == 0; }

  // Returns information on the last-level cache.
  const CpuCacheLevelInfo& back() const {
    ZX_DEBUG_ASSERT(size_ > 0);
    return caches_[size_ - 1];
  }

 private:
  // A split L1 and unified L2, L3, L4 caches makes five.
  static constexpr size_t kMaxNumCaches = 5;

  // We templatize this so that we can supply either the Intel or AMD V2 cache
  // topology leaves (0x4 and 0x8000'001d, respectively), which are identically
  // laid out.
  template <template <uint32_t> class CacheTopologyA,  //
            template <uint32_t> class CacheTopologyB,  //
            template <uint32_t> class CacheTopologyC,  //
            typename MaximumLeaf,                      //
            typename CpuidIoProvider>
  bool TryV2Topology(CpuidIoProvider&& io) {
    if (!CpuidSupports<CacheTopologyA<0>>(io)) {
      return false;
    }

    for (size_t i = 0; i < kMaxNumCaches; ++i, ++size_) {
      const auto eax = Read<CacheTopologyA>(io, i);
      const auto ebx = Read<CacheTopologyB>(io, i);
      const auto ecx = Read<CacheTopologyC>(io, i);

      if (eax.cache_type() == X86CacheType::kNull) {
        break;
      }
      const size_t size_bytes = (ebx.ways() + 1) * (ebx.physical_line_partitions() + 1) *
                                (ebx.system_coherency_line_size() + 1) * (ecx.sets() + 1);
      caches_[i] = {
          .level = eax.cache_level(),
          .type = eax.cache_type(),
          .size_kb = size_bytes / 1024,
          .number_of_sets = ecx.sets() + 1,
          .ways_of_associativity = ebx.ways() + 1,
          .fully_associative = eax.fully_associative() != 0,
          .share_id_shift = CeilLog2(eax.max_sharing_logical_processors() + 1),
      };
    }
    // We expect at least split L1 caches and a L2 cache. If, for whatever reason,
    // less than expected was encoded, fall back to other means to populate
    // `caches`.
    return size_ >= 3;
  }

  // A shim to dynamically look up statically parametrized values.
  template <template <uint32_t> class CpuidValueType,  //
            typename CpuidIoProvider>
  static auto Read(CpuidIoProvider&& io, size_t n) {
    switch (n) {
      case 0:
        return io.template Read<CpuidValueType<0>>();
      case 1:
        return io.template Read<CpuidValueType<1>>();
      case 2:
        return io.template Read<CpuidValueType<2>>();
      case 3:
        return io.template Read<CpuidValueType<3>>();
      case 4:
        return io.template Read<CpuidValueType<4>>();
      default:
        static_assert(kMaxNumCaches == 5);
        ZX_DEBUG_ASSERT(n < kMaxNumCaches);
        __UNREACHABLE;
    }
  }

  static size_t CeilLog2(size_t n) { return cpp20::countr_zero(cpp20::bit_ceil(n)); }

  CpuCacheLevelInfo caches_[kMaxNumCaches];
  // Gives the actual number of `caches_` on which we have information.
  size_t size_ = 0;
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_CACHE_H_
