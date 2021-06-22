// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "arch/x86/system_topology.h"

#include <debug.h>
#include <lib/acpi_lite.h>
#include <lib/acpi_lite/apic.h>
#include <lib/acpi_lite/numa.h>
#include <lib/arch/x86/boot-cpuid.h>
#include <lib/system-topology.h>
#include <pow2.h>
#include <stdio.h>
#include <trace.h>

#include <fbl/alloc_checker.h>
#include <kernel/topology.h>
#include <ktl/optional.h>
#include <ktl/unique_ptr.h>
#include <platform/pc/acpi.h>

#define LOCAL_TRACE 0

namespace {

// TODO(edcoyne): move to fbl::Vector::resize().
template <typename T>
zx_status_t GrowVector(size_t new_size, fbl::Vector<T>* vector) {
  for (size_t i = vector->size(); i < new_size; i++) {
    fbl::AllocChecker checker;
    vector->push_back(T(), &checker);
    if (!checker.check()) {
      return ZX_ERR_NO_MEMORY;
    }
  }
  return ZX_OK;
}

class Core {
 public:
  explicit Core() {
    node_.entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR;
    node_.parent_index = ZBI_TOPOLOGY_NO_PARENT;
    node_.entity.processor.logical_id_count = 0;
    node_.entity.processor.architecture = ZBI_TOPOLOGY_ARCH_X86;
    node_.entity.processor.architecture_info.x86.apic_id_count = 0;
  }

  void SetPrimary(bool primary) {
    node_.entity.processor.flags = (primary) ? ZBI_TOPOLOGY_PROCESSOR_PRIMARY : 0;
  }

  void AddThread(uint16_t logical_id, uint32_t apic_id) {
    auto& processor = node_.entity.processor;
    processor.logical_ids[processor.logical_id_count++] = logical_id;
    processor.architecture_info.x86.apic_ids[processor.architecture_info.x86.apic_id_count++] =
        apic_id;
  }

  void SetFlatParent(uint16_t parent_index) { node_.parent_index = parent_index; }

  const zbi_topology_node_t& node() const { return node_; }

 private:
  zbi_topology_node_t node_;
};

class SharedCache {
 public:
  explicit SharedCache(uint32_t id) {
    node_.entity_type = ZBI_TOPOLOGY_ENTITY_CACHE;
    node_.parent_index = ZBI_TOPOLOGY_NO_PARENT;
    node_.entity.cache.cache_id = id;
  }

  zx_status_t GetCore(int index, Core** core) {
    auto status = GrowVector(index + 1, &cores_);
    if (status != ZX_OK) {
      return status;
    }

    if (!cores_[index]) {
      fbl::AllocChecker checker;
      cores_[index].reset(new (&checker) Core());
      if (!checker.check()) {
        return ZX_ERR_NO_MEMORY;
      }
    }

    *core = cores_[index].get();

    return ZX_OK;
  }

  void SetFlatParent(uint16_t parent_index) { node_.parent_index = parent_index; }

  zbi_topology_node_t& node() { return node_; }

  fbl::Vector<ktl::unique_ptr<Core>>& cores() { return cores_; }

 private:
  zbi_topology_node_t node_;
  fbl::Vector<ktl::unique_ptr<Core>> cores_;
};

class Die {
 public:
  Die() {
    node_.entity_type = ZBI_TOPOLOGY_ENTITY_DIE;
    node_.parent_index = ZBI_TOPOLOGY_NO_PARENT;
  }

  zx_status_t GetCache(int index, SharedCache** cache) {
    auto status = GrowVector(index + 1, &caches_);
    if (status != ZX_OK) {
      return status;
    }

    if (!caches_[index]) {
      fbl::AllocChecker checker;
      caches_[index].reset(new (&checker) SharedCache(index));
      if (!checker.check()) {
        return ZX_ERR_NO_MEMORY;
      }
    }

    *cache = caches_[index].get();

    return ZX_OK;
  }

  zx_status_t GetCore(int index, Core** core) {
    auto status = GrowVector(index + 1, &cores_);
    if (status != ZX_OK) {
      return status;
    }

    if (!cores_[index]) {
      fbl::AllocChecker checker;
      cores_[index].reset(new (&checker) Core());
      if (!checker.check()) {
        return ZX_ERR_NO_MEMORY;
      }
    }

    *core = cores_[index].get();

    return ZX_OK;
  }

  void SetFlatParent(uint16_t parent_index) { node_.parent_index = parent_index; }

  zbi_topology_node_t& node() { return node_; }

  fbl::Vector<ktl::unique_ptr<SharedCache>>& caches() { return caches_; }

  fbl::Vector<ktl::unique_ptr<Core>>& cores() { return cores_; }

  void SetNuma(const acpi_lite::AcpiNumaDomain& numa) { numa_ = {numa}; }

  const ktl::optional<acpi_lite::AcpiNumaDomain>& numa() const { return numa_; }

 private:
  zbi_topology_node_t node_;
  fbl::Vector<ktl::unique_ptr<SharedCache>> caches_;
  fbl::Vector<ktl::unique_ptr<Core>> cores_;
  ktl::optional<acpi_lite::AcpiNumaDomain> numa_;
};

// Unlike the other topological levels, `Package`(/socket) does not define an
// explicit node in the synthesized topology; it serves here merely as a means
// of organizing dies.
class Package {
 public:
  Package() = default;

  zx_status_t GetDie(int index, Die** die) {
    auto status = GrowVector(index + 1, &dies_);
    if (status != ZX_OK) {
      return status;
    }

    if (!dies_[index]) {
      fbl::AllocChecker checker;
      dies_[index].reset(new (&checker) Die());
      if (!checker.check()) {
        return ZX_ERR_NO_MEMORY;
      }
    }

    *die = dies_[index].get();

    return ZX_OK;
  }

  fbl::Vector<ktl::unique_ptr<Die>>& dies() { return dies_; }

 private:
  fbl::Vector<ktl::unique_ptr<Die>> dies_;
};

class PackageList {
 public:
  PackageList(const arch::ApicIdDecoder& decoder, uint32_t primary_apic_id,
              ktl::optional<size_t> last_level_cache_id_shift)
      : decoder_(decoder),
        last_level_cache_id_shift_(last_level_cache_id_shift),
        primary_apic_id_(primary_apic_id) {
    if (!last_level_cache_id_shift.has_value()) {
      dprintf(CRITICAL, "WARNING: could not determine LLC share ID shift\n");
    }
  }

  zx_status_t Add(const acpi_lite::AcpiMadtLocalApicEntry& entry) {
    const uint32_t apic_id = entry.apic_id;
    const bool is_primary = primary_apic_id_ == apic_id;

    const uint32_t pkg_id = decoder_.package_id(apic_id);
    const uint32_t die_id = decoder_.die_id(apic_id);
    const uint32_t core_id = decoder_.core_id(apic_id);
    const uint32_t smt_id = decoder_.smt_id(apic_id);
    const ktl::optional<uint32_t> cache_id =
        last_level_cache_id_shift_.has_value()
            ? ktl::make_optional(apic_id >> *last_level_cache_id_shift_)
            : ktl::nullopt;

    if (pkg_id >= packages_.size()) {
      zx_status_t status = GrowVector(pkg_id + 1, &packages_);
      if (status != ZX_OK) {
        return status;
      }
    }
    auto& pkg = packages_[pkg_id];
    if (!pkg) {
      fbl::AllocChecker checker;
      pkg.reset(new (&checker) Package());
      if (!checker.check()) {
        return ZX_ERR_NO_MEMORY;
      }
    }

    Die* die = nullptr;
    zx_status_t status = pkg->GetDie(die_id, &die);
    if (status != ZX_OK) {
      return status;
    }

    SharedCache* cache = nullptr;
    if (cache_id.has_value()) {
      status = die->GetCache(*cache_id, &cache);
      if (status != ZX_OK) {
        return status;
      }
    }

    Core* core = nullptr;
    status = (cache != nullptr) ? cache->GetCore(core_id, &core) : die->GetCore(core_id, &core);
    if (status != ZX_OK) {
      return status;
    }

    const uint16_t logical_id = is_primary ? 0 : next_logical_id_++;
    core->SetPrimary(is_primary);
    core->AddThread(logical_id, apic_id);

    dprintf(INFO, "APIC: %#4x | Logical: %2u | Package: %2u | Die: %2u | Core: %2u | Thread: %2u |",
            apic_id, logical_id, pkg_id, die_id, core_id, smt_id);
    if (cache_id.has_value()) {
      dprintf(INFO, " LLC: %2u |", *cache_id);
    } else {
      dprintf(INFO, " LLC: %2s |", "?");
    }
    dprintf(INFO, "\n");
    return ZX_OK;
  }

  fbl::Vector<ktl::unique_ptr<Package>>& packages() { return packages_; }

 private:
  fbl::Vector<ktl::unique_ptr<Package>> packages_;
  const arch::ApicIdDecoder& decoder_;
  const ktl::optional<size_t> last_level_cache_id_shift_;
  // APIC ID of this processor, we will ensure it has logical_id 0;
  const uint32_t primary_apic_id_;
  uint16_t next_logical_id_ = 1;
};

zx_status_t GenerateTree(const arch::ApicIdDecoder& decoder,    //
                         uint32_t primary_apic_id,              //
                         const arch::CpuCacheInfo& cache_info,  //
                         const acpi_lite::AcpiParserInterface& parser,
                         fbl::Vector<ktl::unique_ptr<Package>>* packages) {
  PackageList pkg_list(decoder, primary_apic_id,
                       cache_info.empty() ? std::nullopt : cache_info.back().share_id_shift);
  zx_status_t status = acpi_lite::EnumerateProcessorLocalApics(
      parser,
      [&pkg_list](const acpi_lite::AcpiMadtLocalApicEntry& value) { return pkg_list.Add(value); });
  if (status != ZX_OK) {
    return status;
  }

  *packages = ktl::move(pkg_list.packages());
  return ZX_OK;
}

zx_status_t AttachNumaInformation(const acpi_lite::AcpiParserInterface& parser,
                                  const arch::ApicIdDecoder& decoder,
                                  fbl::Vector<ktl::unique_ptr<Package>>* packages) {
  return acpi_lite::EnumerateCpuNumaPairs(
      parser, [&decoder, packages](const acpi_lite::AcpiNumaDomain& domain, uint32_t apic_id) {
        const uint32_t pkg_id = decoder.package_id(apic_id);
        auto& pkg = (*packages)[pkg_id];
        if (!pkg) {
          dprintf(CRITICAL, "ERROR: could not find package #%u", pkg_id);
          return;
        }
        const uint32_t die_id = decoder.die_id(apic_id);
        Die* die = nullptr;
        if (zx_status_t status = pkg->GetDie(die_id, &die); status != ZX_OK) {
          dprintf(CRITICAL, "ERROR: could not find die #%u within package #%u", die_id, pkg_id);
          return;
        }
        if (die && !die->numa()) {
          die->SetNuma(domain);
        }
      });
}

zbi_topology_node_t ToFlatNode(const acpi_lite::AcpiNumaDomain& numa) {
  zbi_topology_node_t flat;
  flat.entity_type = ZBI_TOPOLOGY_ENTITY_NUMA_REGION;
  flat.parent_index = ZBI_TOPOLOGY_NO_PARENT;
  if (numa.memory_count > 0) {
    const auto& mem = numa.memory[0];
    flat.entity.numa_region.start_address = mem.base_address;
    flat.entity.numa_region.end_address = mem.base_address + mem.length;
  }
  return flat;
}

zx_status_t FlattenTree(const fbl::Vector<ktl::unique_ptr<Package>>& packages,
                        fbl::Vector<zbi_topology_node_t>* flat) {
  fbl::AllocChecker checker;
  for (auto& pkg : packages) {
    if (!pkg) {
      continue;
    }

    for (auto& die : pkg->dies()) {
      if (!die) {
        continue;
      }

      if (die->numa()) {
        const auto numa_flat_index = static_cast<uint16_t>(flat->size());
        flat->push_back(ToFlatNode(*die->numa()), &checker);
        if (!checker.check()) {
          return ZX_ERR_NO_MEMORY;
        }
        die->SetFlatParent(numa_flat_index);
      }

      const auto die_flat_index = static_cast<uint16_t>(flat->size());
      flat->push_back(die->node(), &checker);
      if (!checker.check()) {
        return ZX_ERR_NO_MEMORY;
      }

      auto insert_core = [&](const ktl::unique_ptr<Core>& core) {
        if (!core) {
          return ZX_OK;
        }

        flat->push_back(core->node(), &checker);
        if (!checker.check()) {
          return ZX_ERR_NO_MEMORY;
        }
        return ZX_OK;
      };

      for (auto& cache : die->caches()) {
        if (!cache) {
          continue;
        }

        cache->SetFlatParent(die_flat_index);
        const auto cache_flat_index = static_cast<uint16_t>(flat->size());
        flat->push_back(cache->node(), &checker);
        if (!checker.check()) {
          return ZX_ERR_NO_MEMORY;
        }

        // Add cores that are on a die with shared cache.
        for (auto& core : cache->cores()) {
          if (!core) {
            continue;
          }

          core->SetFlatParent(cache_flat_index);
          auto status = insert_core(core);
          if (status != ZX_OK) {
            return status;
          }
        }
      }

      // Add cores directly attached to die.
      for (auto& core : die->cores()) {
        if (!core) {
          continue;
        }

        core->SetFlatParent(die_flat_index);
        auto status = insert_core(core);
        if (status != ZX_OK) {
          return status;
        }
      }
    }
  }
  return ZX_OK;
}

// clang-format off
static constexpr zbi_topology_node_t kFallbackTopology = {
    .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
    .parent_index = ZBI_TOPOLOGY_NO_PARENT,
    .entity = {
      .processor = {
        .logical_ids = {0},
        .logical_id_count = 1,
        .flags = ZBI_TOPOLOGY_PROCESSOR_PRIMARY,
        .architecture = ZBI_TOPOLOGY_ARCH_X86,
        .architecture_info = {
          .x86 = {
            .apic_ids = {0},
            .apic_id_count = 1,
          }
        }
      }
    }
};
// clang-format on

zx_status_t GenerateAndInitSystemTopology(const acpi_lite::AcpiParserInterface& parser) {
  fbl::Vector<zbi_topology_node_t> topology;

  const auto status = x86::GenerateFlatTopology(arch::BootCpuidIo{}, parser, &topology);
  if (status != ZX_OK) {
    dprintf(CRITICAL, "ERROR: failed to generate flat topology from cpuid and acpi data! : %d\n",
            status);
    return status;
  }

  return system_topology::Graph::InitializeSystemTopology(topology.data(), topology.size());
}

}  // namespace

namespace x86::internal {

zx_status_t GenerateFlatTopology(const arch::ApicIdDecoder& decoder,  //
                                 uint32_t primary_apic_id,            //
                                 const arch::CpuCacheInfo& cache_info,
                                 const acpi_lite::AcpiParserInterface& parser,
                                 fbl::Vector<zbi_topology_node_t>* topology) {
  fbl::Vector<ktl::unique_ptr<Package>> pkgs;
  auto status = GenerateTree(decoder, primary_apic_id, cache_info, parser, &pkgs);
  if (status != ZX_OK) {
    return status;
  }

  status = AttachNumaInformation(parser, decoder, &pkgs);
  if (status == ZX_ERR_NOT_FOUND) {
    // This is not a critical error. Systems, such as qemu, may not have the
    // tables present to enumerate NUMA information.
    dprintf(INFO, "System topology: Unable to attach NUMA information, missing ACPI tables.\n");
  } else if (status != ZX_OK) {
    return status;
  }

  return FlattenTree(pkgs, topology);
}

}  // namespace x86::internal

void topology_init() {
  auto status = GenerateAndInitSystemTopology(GlobalAcpiLiteParser());
  if (status != ZX_OK) {
    dprintf(CRITICAL,
            "ERROR: Auto topology generation failed, falling back to only boot core! status: %d\n",
            status);
    status = system_topology::Graph::InitializeSystemTopology(&kFallbackTopology, 1);
    ZX_ASSERT(status == ZX_OK);
  }
}
