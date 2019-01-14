// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/system_topology.h>

#include <algorithm>

#include <lib/system-topology.h>
#include <pow2.h>
#include <printf.h>
#include <trace.h>
#include <ktl/unique_ptr.h>

#define LOCAL_TRACE 0
namespace x86 {
namespace {

using cpu_id::Topology;

// TODO(edcoyne): move to fbl::Vector::resize().
template <typename T>
zx_status_t GrowVector(size_t new_size, fbl::Vector<T>* vector, fbl::AllocChecker* checker) {
    for (size_t i = vector->size(); i < new_size; i++) {
        vector->push_back(T(), checker);
        if (!checker->check()) {
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

    void SetFlatParent(uint16_t parent_index) {
        node_.parent_index = parent_index;
    }

    const zbi_topology_node_t& node() const { return node_; }

private:
    zbi_topology_node_t node_;
};

class SharedCache {
public:
    SharedCache() {
        node_.entity_type = ZBI_TOPOLOGY_ENTITY_CACHE;
        node_.parent_index = ZBI_TOPOLOGY_NO_PARENT;
    }

    zx_status_t GetCore(int index, Core** core, fbl::AllocChecker* checker) {
        auto status = GrowVector(index + 1, &cores_, checker);
        if (status != ZX_OK) {
            return status;
        }

        if (!cores_[index]) {
            cores_[index].reset(new (checker) Core());
            if (!checker->check()) {
                return ZX_ERR_NO_MEMORY;
            }
        }

        *core = cores_[index].get();

        return ZX_OK;
    }

    void SetFlatParent(uint16_t parent_index) {
        node_.parent_index = parent_index;
    }

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

    zx_status_t GetCache(int index, SharedCache** cache, fbl::AllocChecker* checker) {
        auto status = GrowVector(index + 1, &caches_, checker);
        if (status != ZX_OK) {
            return status;
        }

        if (!caches_[index]) {
            caches_[index].reset(new (checker) SharedCache());
            if (!checker->check()) {
                return ZX_ERR_NO_MEMORY;
            }
        }

        *cache = caches_[index].get();

        return ZX_OK;
    }

    void SetFlatParent(uint16_t parent_index) {
        node_.parent_index = parent_index;
    }

    zbi_topology_node_t& node() { return node_; }

    fbl::Vector<ktl::unique_ptr<SharedCache>>& caches() { return caches_; }

    void SetNuma(const AcpiNumaRegion& numa) {
        numa_ = {numa};
    }

    const std::optional<AcpiNumaRegion>& numa() const { return numa_; }

private:
    zbi_topology_node_t node_;
    fbl::Vector<ktl::unique_ptr<SharedCache>> caches_;
    std::optional<AcpiNumaRegion> numa_;
};

class ApicDecoder {
public:
    ApicDecoder(uint8_t smt, uint8_t core, uint8_t cache)
        : smt_bits_(smt), core_bits_(core), cache_shift_(cache){}

    static std::optional<ApicDecoder> From(const cpu_id::CpuId& cpuid) {
        uint8_t smt_bits = 0, core_bits = 0, cache_shift = 0;

        const auto topology = cpuid.ReadTopology();
        const auto cache = topology.highest_level_cache();
        cache_shift = cache.shift_width;
        LTRACEF("Top cache level: %u size: %lu\n", cache.level, cache.size_bytes);

        const auto levels_opt = topology.levels();
        if (!levels_opt) {
            return {};
        }

        const auto& levels = *levels_opt;
        for (int i = 0; i < levels.level_count; i++) {
            const auto& level = levels.levels[i];
            switch (level.type) {
            case Topology::LevelType::SMT:
                smt_bits = level.id_bits;
                break;
            case Topology::LevelType::CORE:
                core_bits = level.id_bits;
                break;
            default:
                break;
            }
        }
        LTRACEF("smt_bits: %u core_bits: %u cache_shift: %u \n",
                smt_bits, core_bits, cache_shift);
        return {ApicDecoder(smt_bits, core_bits, cache_shift)};
    }

    uint32_t smt_id(uint32_t apic_id) const {
        return apic_id & ToMask(smt_bits_);
    }

    uint32_t core_id(uint32_t apic_id) const {
        return (apic_id >> smt_bits_) & ToMask(core_bits_);
    }

    uint32_t die_id(uint32_t apic_id) const {
        // The die (or package) is defined by Intel as being what is left over
        // after all other level ids are extracted.
        return apic_id >> (smt_bits_ + core_bits_);
    }

    uint32_t cache_id(uint32_t apic_id) const {
        return apic_id >> cache_shift_;
    }

private:
    uint32_t ToMask(uint8_t width) const {
        return valpow2(width) - 1;
    }

    const uint8_t smt_bits_;
    const uint8_t core_bits_;
    const uint8_t cache_shift_;
};

zx_status_t GenerateTree(const cpu_id::CpuId& cpuid, const AcpiTables& acpi_tables,
                         const ApicDecoder& decoder, fbl::Vector<ktl::unique_ptr<Die>>* dies) {
    uint32_t cpu_count = 0;
    auto status = acpi_tables.cpu_count(&cpu_count);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AllocChecker checker;
    ktl::unique_ptr<uint32_t[]> apic_ids(new (&checker) uint32_t[cpu_count]);
    if (!checker.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    uint32_t apic_ids_count = 0;
    status = acpi_tables.cpu_apic_ids(apic_ids.get(), cpu_count, &apic_ids_count);
    if (status != ZX_OK) {
        return status;
    }
    DEBUG_ASSERT(apic_ids_count == cpu_count);

    // APIC of this processor, we will ensure it has logical_id 0;
    const uint32_t primary_apic_id = cpuid.ReadProcessorId().local_apic_id();

    uint16_t next_logical_id = 1;
    for (size_t i = 0; i < apic_ids_count; i++) {
        const auto apic_id = apic_ids[i];
        const bool is_primary = primary_apic_id == apic_id;

        const uint32_t die_id = decoder.die_id(apic_id);

        if (die_id >= dies->size()) {
            GrowVector(die_id + 1, dies, &checker);
        }
        auto& die = (*dies)[die_id];
        if (!die) {
            die.reset(new (&checker) Die());
            if (!checker.check()) {
                return ZX_ERR_NO_MEMORY;
            }
        }

        SharedCache* cache = nullptr;
        status = die->GetCache(decoder.cache_id(apic_id), &cache, &checker);
        if (status != ZX_OK) {
            return status;
        }

        Core* core = nullptr;
        status = cache->GetCore(decoder.core_id(apic_id), &core, &checker);
        if (status != ZX_OK) {
            return status;
        }

        const uint16_t logical_id = (is_primary) ? 0 : next_logical_id++;
        core->SetPrimary(is_primary);
        core->AddThread(logical_id, apic_id);

        LTRACEF("apic: %X logical: %u package: %u \n", apic_id, logical_id, die_id);
    }

    return ZX_OK;
}

zx_status_t AttachNumaInformation(const AcpiTables& acpi_tables, const ApicDecoder& decoder,
                                  fbl::Vector<ktl::unique_ptr<Die>>* dies) {
    return acpi_tables.VisitCpuNumaPairs(
        [&decoder, dies](const AcpiNumaRegion& region, uint32_t apic_id) {
            auto& die = (*dies)[decoder.die_id(apic_id)];
            if (die && !die->numa()) {
                die->SetNuma(region);
            }
        });
}

zbi_topology_node_t ToFlatNode(const AcpiNumaRegion& numa) {
    zbi_topology_node_t flat;
    flat.entity_type = ZBI_TOPOLOGY_ENTITY_NUMA_REGION;
    flat.parent_index = ZBI_TOPOLOGY_NO_PARENT;
    flat.entity.numa_region.start_address = numa.base_address;
    flat.entity.numa_region.end_address = numa.base_address + numa.length;
    return flat;
}

zx_status_t FlattenTree(const fbl::Vector<ktl::unique_ptr<Die>>& dies,
                        fbl::Vector<zbi_topology_node_t>* flat) {
    fbl::AllocChecker checker;
    for (auto& die : dies) {
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

            for (auto& core : cache->cores()) {
                if (!core) {
                    continue;
                }

                core->SetFlatParent(cache_flat_index);
                flat->push_back(core->node(), &checker);
                if (!checker.check()) {
                    return ZX_ERR_NO_MEMORY;
                }
            }
        }
    }
    return ZX_OK;
}

} // namespace

zx_status_t GenerateFlatTopology(const cpu_id::CpuId& cpuid, const AcpiTables& acpi_tables,
                                 fbl::Vector<zbi_topology_node_t>* topology) {
    const auto decoder_opt = ApicDecoder::From(cpuid);
    if (!decoder_opt) {
        return ZX_ERR_INTERNAL;
    }
    const auto& decoder = *decoder_opt;
    fbl::Vector<ktl::unique_ptr<Die>> dies;
    auto status = GenerateTree(cpuid, acpi_tables, decoder, &dies);
    if (status != ZX_OK) {
        return status;
    }

    status = AttachNumaInformation(acpi_tables, decoder, &dies);
    if (status != ZX_OK) {
        return status;
    }

    return FlattenTree(dies, topology);
}

} // namespace x86

void x86_system_topology_init() {
    const AcpiTableProvider table_provider;
    fbl::Vector<zbi_topology_node_t> topology;
    auto status = x86::GenerateFlatTopology(cpu_id::CpuId(), AcpiTables(&table_provider),
                                            &topology);
    status = system_topology::GetMutableSystemTopology().Update(topology.get(), topology.size());
    if (status != ZX_OK) {
        panic("Failed to initialize system topology!");
    }
}
