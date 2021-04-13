// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_APIC_ID_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_APIC_ID_H_

#include <lib/arch/x86/cpuid.h>
#include <lib/stdcompat/bit.h>

namespace arch {

// Returns the APIC ID - x2APIC if supported - associated with the logical
// processor in turn associated with the provided CpuidIoProvider.
template <typename CpuidIoProvider>
inline uint32_t GetApicId(CpuidIoProvider&& io) {
  using LevelType = CpuidTopologyEnumerationC::TopologyLevelType;

  // [intel/vol3]: 8.9.2  Hierarchical Mapping of CPUID Extended Topology Leaf.
  //
  // For extended topology enumeration, if the first level does not encode the
  // "SMT" level (a spec'ed expecation), then we assume the associated leaves
  // to be invalid.
  if (CpuidSupports<CpuidV2TopologyEnumerationA<0>>(io) &&
      io.template Read<CpuidV2TopologyEnumerationC<0>>().level_type() == LevelType::kSmt) {
    return io.template Read<CpuidV2TopologyEnumerationD<0>>().x2apic_id();
  }
  if (CpuidSupports<CpuidV1TopologyEnumerationA<0>>(io) &&
      io.template Read<CpuidV1TopologyEnumerationC<0>>().level_type() == LevelType::kSmt) {
    return io.template Read<CpuidV1TopologyEnumerationD<0>>().x2apic_id();
  }

  if (CpuidSupports<CpuidExtendedApicId>(io)) {
    return io.template Read<CpuidExtendedApicId>().x2apic_id();
  }

  return io.template Read<CpuidProcessorInfo>().initial_apic_id();
}

// ApicIdDecoder is a utility for extracting particular topological level
// IDs from an (x2)APIC ID.
//
// In full generality, an APIC ID might decompose as follows:
//
// [intel/vol3]: Figure 8-5.  Generalized Seven Level Interpretation of the APIC ID.
// -----------------------------------------------------------------------------
// | CLUSTER ID | PACKAGE ID | DIE ID | TILE ID | MODULE ID | CORE ID | SMT ID |
// -----------------------------------------------------------------------------
//
// where the full ID width is 32-bit (if x2APIC) or 8-bit.
//
// This, however, is higher fidelity than we are able to make use of. Since
// CLUSTER ID and PACKAGE_ID are not directly enumerable from CPUID, we elide
// the two IDs into a single PACKAGE ID, defined as the rest of the ID above
// DIE. Moreover, the system currently has no use for enumerating tiles and
// modules directly (which is also a practice that AMD does not do): we elide
// the TILE and MODULE IDs into DIE ID alone. Accordingly, ApicIdDecoder
// partitions up the APIC address space as
// ------------------------------------------
// | PACKAGE ID | DIE ID | CORE ID | SMT ID |
// ------------------------------------------
class ApicIdDecoder {
 public:
  template <typename CpuidIoProvider,
            // To avoid precedence over copy and move constructors.
            typename = std::enable_if_t<!std::is_same_v<CpuidIoProvider, ApicIdDecoder>>>
  explicit ApicIdDecoder(CpuidIoProvider&& io) {
    // [intel/vol3]: Example 8-21.  Support Routines for Identifying Package,
    // Core and Logical Processors from 8-bit Initial APIC ID.
    // [amd/vol3]: E.5.1  Legacy Method.
    //
    // When HTT ("Hyper-Threading Technology") is not advertised, the package
    // contains a single logical processor. This is counter-intuitive, but
    // Intel cores that do not actually have SMT available may still present
    // HTT == 1; moreover, in the case of AMD, HTT means "either that there is
    // more than one thread per core or more than one core per compute unit".
    if (!io.template Read<CpuidFeatureFlagsD>().htt()) {
      return;
    }

    // First try the extended topology leaves, which may work with older AMD
    // models. The "V2" leaf 0x1f is preferred - if available - to the "V1"
    // leaf 0xb.
    if (TryExtendedTopology<CpuidV2TopologyEnumerationA, CpuidV2TopologyEnumerationC>(io) ||
        TryExtendedTopology<CpuidV1TopologyEnumerationA, CpuidV1TopologyEnumerationC>(io)) {
      // The DIE level might not have been explicitly enumerated. If it does
      // not seem so, redefine the cumulatve die-and-below ID width to be the
      // rounded binary order of the maximum number of addressable logical
      // processors per package, which should always coincide in general.
      if (die_id_cumulative_width_ == core_id_cumulative_width_) {
        die_id_cumulative_width_ = CeilLog2(MaxNumLogicalProcessors(io));
      }
      return;
    }

    // Maximum per package, that is.
    size_t max_logical_processors = MaxNumLogicalProcessors(io);
    size_t max_cores = 1;
    size_t max_dies = 1;
    auto finalize = [&]() {
      if (!(max_logical_processors >= max_cores && max_cores >= max_dies && max_dies > 0)) {
        return;
      }

      smt_id_width_ = CeilLog2(max_logical_processors / max_cores);
      core_id_cumulative_width_ = CeilLog2(max_cores / max_dies) + smt_id_width_;
      die_id_cumulative_width_ = CeilLog2(max_logical_processors);
    };

    // [intel/vol3]: Example 8-21.  Support Routines for Identifying
    // Package, Core and Logical Processors from 8-bit Initial APIC ID.
    if (CpuidSupports<CpuidIntelCacheTopologyA<0>>(io)) {
      const auto zeroth_cache_topology = io.template Read<CpuidIntelCacheTopologyA<0>>();
      if (zeroth_cache_topology.cache_type() != X86CacheType::kNull) {
        // The field encodes one less than the real count.
        max_cores = zeroth_cache_topology.max_cores() + 1;
        finalize();
        return;
      }
    }

    // Unfortunately, the AMD spec does not give a general way of
    // determining the maximum number of addressable cores and dies per
    // package, respectively. If leaf 0x8000'001e is supported (which
    // requires the topology extension feature to be advertised), then we
    // can give best-effort guesses of these quanities based on the actual
    // counts of dies per package and logical processors per core.
    if (CpuidSupports<CpuidComputeUnitInfo>(io)) {
      // We translate "compute unit" and "node" here as core and die,
      // respectively.
      max_dies = io.template Read<CpuidNodeInfo>().nodes_per_package() + 1;
      const size_t threads_per_core =
          io.template Read<CpuidComputeUnitInfo>().threads_per_compute_unit() + 1;
      max_cores = max_logical_processors / threads_per_core;
    }
    finalize();
  }

  ApicIdDecoder() = delete;

  uint32_t smt_id(uint32_t apic_id) const { return apic_id & ToMask(smt_id_width_); }

  uint32_t core_id(uint32_t apic_id) const {
    return (apic_id & ToMask(core_id_cumulative_width_)) >> smt_id_width_;
  }

  uint32_t die_id(uint32_t apic_id) const {
    return (apic_id & ToMask(die_id_cumulative_width_)) >> core_id_cumulative_width_;
  }

  uint32_t package_id(uint32_t apic_id) const { return apic_id >> die_id_cumulative_width_; }

 private:
  using TopologyLevelType = CpuidTopologyEnumerationC::TopologyLevelType;

  static constexpr size_t kMaxTopologyLevel = static_cast<size_t>(TopologyLevelType::kDie);

  // [intel/vol3]: Example 8-18.  Support Routines for Identifying Package,
  // Die, Core and Logical Processors from 32-bit x2APIC ID.
  //
  // Attempts to perform Intel's extended topology enumeration routine and
  // returns whether the attempt was successful. We templatize this so that we
  // can supply the CPUID value types of either V1 and V2 leaves (0x1f and 0xb,
  // respectively), which are identically laid out.
  template <template <uint32_t> class TopologyEnumerationA,
            template <uint32_t> class TopologyEnumerationC,  //
            typename CpuidIoProvider>
  bool TryExtendedTopology(CpuidIoProvider&& io) {
    if (!CpuidSupports<TopologyEnumerationA<0>>(io)) {
      return false;
    }

    for (size_t i = 0; i < kMaxTopologyLevel; ++i) {
      const auto eax = Read<TopologyEnumerationA>(io, i);
      const auto ecx = Read<TopologyEnumerationC>(io, i);

      // The above reference explains that SMT is expected to be the first
      // level.
      const auto level_type = ecx.level_type();
      if (i == 0 && level_type != TopologyLevelType::kSmt) {
        return false;
      }
      const auto shift = eax.next_level_apic_id_shift();
      switch (level_type) {
        case TopologyLevelType::kInvalid:
          return true;  // Signals the end of iteration.
        case TopologyLevelType::kSmt:
          smt_id_width_ = shift;
          core_id_cumulative_width_ = shift;
          die_id_cumulative_width_ = shift;
          break;
        case TopologyLevelType::kCore:
          core_id_cumulative_width_ = shift;
          die_id_cumulative_width_ = shift;
          break;
        // See class documentation regarding the elision of MODULE and TILE.
        case TopologyLevelType::kModule:
        case TopologyLevelType::kTile:
        case TopologyLevelType::kDie:
          die_id_cumulative_width_ = shift;
          break;
      }
    }

    // Something went wrong; iteration should have finished in hitting on a
    // kInvalid level.
    return false;
  }

  // A shim to dynamically look up statically parametrized values.
  template <template <uint32_t> class TopologyEnumeration, typename CpuidIoProvider>
  static auto Read(CpuidIoProvider&& io, size_t n) {
    switch (n) {
      case 0:
        return io.template Read<TopologyEnumeration<0>>();
      case 1:
        return io.template Read<TopologyEnumeration<1>>();
      case 2:
        return io.template Read<TopologyEnumeration<2>>();
      case 3:
        return io.template Read<TopologyEnumeration<3>>();
      case 4:
        return io.template Read<TopologyEnumeration<4>>();
      case 5:
        return io.template Read<TopologyEnumeration<5>>();
      default:
        static_assert(kMaxTopologyLevel == 5);
        ZX_DEBUG_ASSERT(n <= kMaxTopologyLevel);
        __UNREACHABLE;
    }
  }

  // Returns the maximum addressible number of logical processors per package.
  // Both Intel and AMD spec ways to determine this quantity.
  template <typename CpuidIoProvider>
  size_t MaxNumLogicalProcessors(CpuidIoProvider&& io) {
    // The Intel max.
    size_t max = io.template Read<CpuidProcessorInfo>().max_logical_processors();

    // The AMD max. For AMD hardware, the quantity above gives the actual count
    // of logical processors instead of the maximum number of addressible ones.
    if (CpuidSupports<CpuidExtendedSizeInfo>(io)) {
      // [amd/vol3]: E.5.2  Extended Method.
      const auto size_ids = io.template Read<CpuidExtendedSizeInfo>();
      const size_t amd_max =
          size_ids.apic_id_size() ? (1 << size_ids.apic_id_size()) : size_ids.nc() + 1;
      max = std::max(amd_max, max);
    }
    return max;
  }

  static size_t CeilLog2(size_t n) { return cpp20::countr_zero(cpp20::bit_ceil(n)); }

  static uint32_t ToMask(size_t width) { return ~(uint32_t{0xffffffff} << width); }

  size_t smt_id_width_ = 0;
  // CORE ID width + SMT ID width.
  size_t core_id_cumulative_width_ = 0;
  // DIE ID width + CORE ID width + SMT ID width.
  size_t die_id_cumulative_width_ = 0;
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_APIC_ID_H_
