// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iommu.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <optional>

#include <acpica/acpi.h>
#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/span.h>

namespace {

// Temporary single global reference until C wrappers are removed.
x86::IommuManager* iommu_mgr = nullptr;

bool use_hardware_iommu(void) {
  const char* value = getenv("driver.iommu.enable");
  if (value == NULL) {
    return false;  // Default to false currently
  } else if (!strcmp(value, "0") || !strcmp(value, "false") || !strcmp(value, "off")) {
    return false;
  } else {
    return true;
  }
}

// Given a table that may have a Length > sizeof(TABLE), this returns a Span of the data following
// table, based on that Length. The type T of the record can be used to get a more convenient span
// and will cause the alignment etc to ber checked.
template <typename T, bool HEADER = true, typename TABLE>
std::optional<fbl::Span<const T>> record_span(const TABLE* table) {
  const uintptr_t records_start = reinterpret_cast<uintptr_t>(table) + sizeof(*table);
  size_t size;
  if constexpr (HEADER) {
    size = table->Header.Length;
  } else {
    size = table->Length;
  }
  const uintptr_t records_end = reinterpret_cast<uintptr_t>(table) + size;

  size_t byte_len = records_end - records_start;
  if ((byte_len % sizeof(T)) != 0 || (records_start % std::alignment_of_v<T>) != 0) {
    return std::nullopt;
  }
  return fbl::Span<const T>{reinterpret_cast<const T*>(records_start), byte_len / sizeof(T)};
}

// Given a TABLE this will iterate over all the variable length records that might follow it.
// That is RECORD::Length is the length of each record. The provided func will be called on every
// record that is found.
template <typename RECORD, typename TABLE, typename FUNC>
zx_status_t for_each_record(const TABLE* table, FUNC func) {
  fbl::Span<const uint8_t> records;
  if (auto r = record_span<uint8_t>(table)) {
    records = std::move(*r);
  } else {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  uintptr_t offset;
  for (offset = 0; offset < records.size();) {
    const RECORD* record_hdr = reinterpret_cast<const RECORD*>(&records[offset]);
    zx_status_t result = func(record_hdr);
    if (result != ZX_ERR_NEXT) {
      return result;
    }

    offset += record_hdr->Length;
  }

  if (offset != records.size()) {
    zxlogf(ERROR, "%s: table length weird: %zu, reached %zu", __func__, records.size(), offset);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  return ZX_OK;
}

// Convenience wrapper around for_each_record that filters for the requested record type and calls
// the provided func with a pointer to the correctly typed table.
template <AcpiDmarType Type, typename F>
zx_status_t for_each_dmar_of_type(const ACPI_TABLE_DMAR* dmar, F func) {
  return for_each_record<ACPI_DMAR_HEADER>(dmar, [func](const ACPI_DMAR_HEADER* h) {
    if (h->Type == Type) {
      if constexpr (Type == ACPI_DMAR_TYPE_HARDWARE_UNIT) {
        const ACPI_DMAR_HARDWARE_UNIT* r = reinterpret_cast<const ACPI_DMAR_HARDWARE_UNIT*>(h);
        return func(r);
      } else if constexpr (Type == ACPI_DMAR_TYPE_RESERVED_MEMORY) {
        const ACPI_DMAR_RESERVED_MEMORY* m = reinterpret_cast<const ACPI_DMAR_RESERVED_MEMORY*>(h);
        return func(m);
      } else {
        static_assert(Type != Type,
                      "for_each_dmar_of_type used for an unknown type. Please add it to the "
                      "if/else-if chain above");
      }
    }
    return ZX_ERR_NEXT;
  });
}

// Converts a scope as described in the ACPI tables to one of the form used by zircon on x86.
zx_status_t acpi_scope_to_desc(const ACPI_DMAR_DEVICE_SCOPE* acpi_scope,
                               zx_iommu_desc_intel_scope_t* desc_scope) {
  switch (acpi_scope->EntryType) {
    case ACPI_DMAR_SCOPE_TYPE_ENDPOINT:
      desc_scope->type = ZX_IOMMU_INTEL_SCOPE_ENDPOINT;
      break;
    case ACPI_DMAR_SCOPE_TYPE_BRIDGE:
      zxlogf(INFO, "acpi-bus: bridge scopes not supported");
      return ZX_ERR_NOT_SUPPORTED;
    default:
      // Skip this scope, since it's not a type we care about.
      return ZX_ERR_WRONG_TYPE;
  }
  desc_scope->start_bus = acpi_scope->Bus;
  if (acpi_scope->Length < sizeof(*acpi_scope)) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  fbl::Span<const uint16_t> hops;
  if (auto h = record_span<uint16_t, false>(acpi_scope)) {
    hops = std::move(*h);
  } else {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  desc_scope->num_hops = static_cast<uint8_t>(hops.size());
  if (countof(desc_scope->dev_func) < desc_scope->num_hops) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  // TODO(teisenbe): We need to be aware of the mapping between
  // PCI paths and bus numbers to properly evaluate this.
  if (desc_scope->num_hops != 1) {
    zxlogf(INFO, "acpi-bus: non root bus devices not supported");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Walk the variable-length array of hops that is appended to the main
  // ACPI_DMAR_DEVICE_SCOPE structure.
  for (ssize_t i = 0; i < desc_scope->num_hops; ++i) {
    uint16_t v = hops[i];
    const uint8_t dev = v & 0x1f;
    const uint8_t func = (v >> 8) & 0x7;
    desc_scope->dev_func[i] = static_cast<uint8_t>((dev << 3) | func);
  }
  return ZX_OK;
}

// Walks the given unit's scopes and calls the supplied closure on each of them. This can be used
// on any TABLE where the records that follow it are scope records.
template <typename TABLE, typename F>
zx_status_t for_each_scope(const TABLE* unit, F func) {
  return for_each_record<ACPI_DMAR_DEVICE_SCOPE>(unit, [func](const ACPI_DMAR_DEVICE_SCOPE* scope) {
    zx_iommu_desc_intel_scope_t intel_scope;
    zx_status_t status = acpi_scope_to_desc(scope, &intel_scope);
    if (status == ZX_ERR_WRONG_TYPE) {
      return ZX_ERR_NEXT;
    }
    if (status == ZX_OK) {
      status = func(intel_scope);
    }
    return status;
  });
}

bool scope_eq(const zx_iommu_desc_intel_scope_t* scope,
              const zx_iommu_desc_intel_scope_t* other_scope) {
  if (scope->type != other_scope->type || scope->start_bus != other_scope->start_bus ||
      scope->num_hops != other_scope->num_hops) {
    return false;
  }

  for (size_t i = 0; i < scope->num_hops; ++i) {
    if (scope->dev_func[i] != other_scope->dev_func[i]) {
      return false;
    }
  }

  return true;
}

// Walks all the reserved memory regions and finds any that match the scopes for the provided
// pci_segment. The append_mem and append_scope callbacks are called on matching scopes/memory
// regions. See CreateDesc for an explanation of scope_func.
template <typename SCOPE_DESC, typename APPEND_MEM, typename APPEND_SCOPE>
zx_status_t process_reserved_mem(const ACPI_TABLE_DMAR* table, uint16_t pci_segment,
                                 bool whole_segment, SCOPE_DESC scope_func, APPEND_MEM append_mem,
                                 APPEND_SCOPE append_scope) {
  return for_each_dmar_of_type<ACPI_DMAR_TYPE_RESERVED_MEMORY>(
      table, [pci_segment, whole_segment, scope_func, append_mem, append_scope](const auto* rec) {
        if (pci_segment != rec->Segment) {
          return ZX_ERR_NEXT;
        }
        bool one_scope = false;
        auto submit_scope = [&one_scope, append_scope, append_mem, base = rec->BaseAddress,
                             end = rec->EndAddress](const auto* scope) {
          if (!one_scope) {
            append_mem(base, end);
            one_scope = true;
          }
          append_scope(scope);
        };

        // Search for scopes that match
        zx_status_t result = for_each_scope<ACPI_DMAR_RESERVED_MEMORY>(
            rec, [&whole_segment, &submit_scope, &scope_func](const auto& s) {
              // TODO(teisenbe): We should skip scope types we don't
              // care about here

              // Search for a scope in the descriptor that matches this
              // ACPI scope.
              bool no_matches = true;
              scope_func([&s, &no_matches, &whole_segment, &submit_scope](const auto& scope) {
                const bool scope_matches = scope_eq(&scope, &s);
                no_matches &= !scope_matches;

                // If this is a whole segment descriptor, then a match
                // corresponds to an entry we should ignore.
                if (scope_matches && !whole_segment) {
                  submit_scope(&scope);
                  return ZX_ERR_STOP;
                }
                return ZX_ERR_NEXT;
              });

              if (no_matches && whole_segment) {
                submit_scope(&s);
              }
              return ZX_ERR_NEXT;
            });
        if (result != ZX_OK) {
          return result;
        }
        return ZX_ERR_NEXT;
      });
}

}  // namespace

namespace x86 {

// Processes enough of the tables to determine how big much memory we need to describe the
// descriptor and allocates + fills in the basic information of the descriptor. See CreateDesc for
// an explanation of ScopeFunc.
template <typename F>
zx_status_t IommuDesc::AllocDesc(const ACPI_TABLE_DMAR* table, uint16_t pci_segment,
                                 bool whole_segment, F scope_func) {
  size_t num_scopes = 0;
  zx_status_t status = scope_func([&num_scopes](const auto& scope) {
    num_scopes++;
    return ZX_ERR_NEXT;
  });
  if (status != ZX_OK) {
    return status;
  }

  size_t num_reserved_mem = 0;
  size_t num_mem_scopes = 0;

  status = process_reserved_mem(
      table, pci_segment, whole_segment, scope_func,
      [&num_reserved_mem](auto base, auto end) { num_reserved_mem++; },
      [&num_mem_scopes](const auto* scope) { num_mem_scopes++; });
  if (status != ZX_OK) {
    return status;
  }

  const size_t reserved_mem_bytes =
      sizeof(zx_iommu_desc_intel_scope_t) * num_mem_scopes +
      sizeof(zx_iommu_desc_intel_reserved_memory_t) * num_reserved_mem;
  const size_t scope_bytes = sizeof(zx_iommu_desc_intel_scope_t) * num_scopes;

  const size_t desc_bytes = sizeof(zx_iommu_desc_intel_t) + scope_bytes + reserved_mem_bytes;

  desc_ = fbl::Array(new uint8_t[desc_bytes], desc_bytes);

  zx_iommu_desc_intel_t* desc = RawDesc();
  desc->scope_bytes = static_cast<uint8_t>(scope_bytes);
  desc->reserved_memory_bytes = static_cast<uint16_t>(reserved_mem_bytes);
  desc->whole_segment = whole_segment;
  return ZX_OK;
}

// Creates descriptor information for the given pci_segment. scope_func is a closure that itself
// takes a closure, which is called on every scope. That is scope_func's signature is roughly:
// zx_status_t (*scope_func)(zx_status_t(*scope_callback)(zx_iommu_desc_intel_scope_t &scope))
// This provides an abstract way to 'do something for every scope' where finding scopes is itself
// something we want to be abstract over.
template <typename F>
zx_status_t IommuDesc::CreateDesc(const ACPI_TABLE_DMAR* table, uint64_t base, uint16_t pci_segment,
                                  bool whole_segment, F scope_func) {
  zx_status_t status = AllocDesc(table, pci_segment, whole_segment, scope_func);
  if (status != ZX_OK) {
    return status;
  }

  zx_iommu_desc_intel_t* desc = RawDesc();
  desc->register_base = base;
  desc->pci_segment = pci_segment;

  size_t scopes_found = 0;
  auto scopes = Scopes();
  status = scope_func([&scopes, &scopes_found](const auto& scope) {
    if (scopes_found > scopes.size()) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    scopes[scopes_found] = scope;
    scopes_found++;
    return ZX_ERR_NEXT;
  });

  if (status != ZX_OK) {
    return status;
  }

  fbl::Span<uint8_t> reserved = ReservedMem();
  zx_iommu_desc_intel_reserved_memory_t* last_mem = nullptr;
  status = process_reserved_mem(
      table, pci_segment, whole_segment, scope_func,
      [&last_mem, &reserved](auto start, auto end) {
        last_mem = reinterpret_cast<zx_iommu_desc_intel_reserved_memory_t*>(reserved.data());
        reserved = reserved.subspan(sizeof(*last_mem));
        last_mem->base_addr = start;
        last_mem->len = end - start + 1;
        last_mem->scope_bytes = 0;
      },
      [&last_mem, &reserved](const auto* scope) {
        auto* new_scope = reinterpret_cast<zx_iommu_desc_intel_scope_t*>(reserved.data());
        reserved = reserved.subspan(sizeof(zx_iommu_desc_intel_scope_t));
        memcpy(new_scope, scope, sizeof(zx_iommu_desc_intel_scope_t));
        last_mem->scope_bytes =
            static_cast<uint8_t>(last_mem->scope_bytes + sizeof(zx_iommu_desc_intel_scope_t));
      });

  return status;
}

zx_status_t IommuDesc::CreateWholeSegmentDesc(const ACPI_TABLE_DMAR* table,
                                              const ACPI_DMAR_HARDWARE_UNIT* unit) {
  assert(unit->Flags & ACPI_DMAR_INCLUDE_ALL);

  // The VT-d spec requires that whole-segment hardware units appear in the
  // DMAR table after all other hardware units on their segment. Search those
  // entries for scopes to specify as excluded from this descriptor.

  auto scope_gen = [&unit, &table](auto f) {
    return for_each_dmar_of_type<ACPI_DMAR_TYPE_HARDWARE_UNIT>(table, [&unit, &f](const auto* rec) {
      if (rec->Segment != unit->Segment) {
        return ZX_ERR_NEXT;
      }
      zx_status_t status = for_each_scope<ACPI_DMAR_HARDWARE_UNIT>(rec, f);
      if (status == ZX_OK) {
        return ZX_ERR_NEXT;
      }
      return status;
    });
  };

  return CreateDesc(table, unit->Address, unit->Segment, true, scope_gen);
}

zx_status_t IommuDesc::CreatePartialSegmentDesc(const ACPI_TABLE_DMAR* table,
                                                const ACPI_DMAR_HARDWARE_UNIT* unit) {
  assert((unit->Flags & ACPI_DMAR_INCLUDE_ALL) == 0);

  auto scope_gen = [&unit](auto f) { return for_each_scope<ACPI_DMAR_HARDWARE_UNIT>(unit, f); };

  return CreateDesc(table, unit->Address, unit->Segment, false, scope_gen);
}

zx_status_t IommuDesc::CreateIommu(const zx::unowned_resource& root_resource) {
  return zx::iommu::create(*root_resource, ZX_IOMMU_TYPE_INTEL, &Desc(), desc_.size(), &iommu_);
}

IommuManager::~IommuManager() {
  if (iommu_mgr == this) {
    iommu_mgr = nullptr;
  }
}

zx_status_t IommuManager::Init(bool force_hardware_iommu) {
  // Prevent double initialization.
  ZX_DEBUG_ASSERT(!iommu_mgr);
  iommu_mgr = this;

  zx_iommu_desc_dummy_t dummy;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx::unowned_resource root_resource(get_root_resource());
  zx_status_t status =
      zx::iommu::create(*root_resource, ZX_IOMMU_TYPE_DUMMY, &dummy, sizeof(dummy), &dummy_iommu_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: error in zx::iommu::create: %d", __func__, status);
    return status;
  }

  if (!force_hardware_iommu && !use_hardware_iommu()) {
    zxlogf(INFO, "%s: not using IOMMU", __func__);
    return ZX_OK;
  }

  ACPI_TABLE_HEADER* table = NULL;
  ACPI_STATUS acpi_status = AcpiGetTable((char*)ACPI_SIG_DMAR, 1, &table);
  if (acpi_status != AE_OK) {
    zxlogf(INFO, "%s: could not find DMAR table", __func__);
    return ZX_ERR_NOT_FOUND;
  }
  ACPI_TABLE_DMAR* dmar = reinterpret_cast<ACPI_TABLE_DMAR*>(table);
  status = InitDesc(dmar);
  if (status != ZX_OK) {
    return status;
  }
  for (auto& iommu : iommus_) {
    // Please do not use get_root_resource() in new code. See ZX-1467.
    status = iommu.CreateIommu(root_resource);
    if (status != ZX_OK) {
      zxlogf(ERROR, "acpi-bus: Failed to create iommu object: %d", status);
      // Reset the iommus_ so that IommuForBdf doesn't try and use them.
      iommus_.reset();
      return status;
    }
  }
  zxlogf(INFO, "acpi-bus: using IOMMU");
  return ZX_OK;
}

zx_status_t IommuManager::InitDesc(const ACPI_TABLE_DMAR* dmar) {
  // Count the IOMMUs
  size_t num_iommus = 0;
  zx_status_t status =
      for_each_dmar_of_type<ACPI_DMAR_TYPE_HARDWARE_UNIT>(dmar, [&num_iommus](const auto* r) {
        num_iommus++;
        return ZX_ERR_NEXT;
      });
  if (status != ZX_OK) {
    return status;
  }

  if (num_iommus == 0) {
    return ZX_OK;
  }

  fbl::Array<IommuDesc> iommus = fbl::Array<IommuDesc>(new IommuDesc[num_iommus], num_iommus);

  size_t iommu_idx = 0;

  status = for_each_record<ACPI_DMAR_HEADER>(
      dmar, [&dmar, &iommu_idx, &iommus](const ACPI_DMAR_HEADER* record_hdr) {
        zxlogf(DEBUG1, "DMAR record: %d", record_hdr->Type);
        switch (record_hdr->Type) {
          case ACPI_DMAR_TYPE_HARDWARE_UNIT: {
            const auto* rec = reinterpret_cast<const ACPI_DMAR_HARDWARE_UNIT*>(record_hdr);

            zxlogf(DEBUG1, "DMAR Hardware Unit: %u %#llx %#x", rec->Segment, rec->Address,
                   rec->Flags);
            const bool whole_segment = rec->Flags & ACPI_DMAR_INCLUDE_ALL;

            zx_status_t status;
            if (whole_segment) {
              status = iommus[iommu_idx].CreateWholeSegmentDesc(dmar, rec);
            } else {
              status = iommus[iommu_idx].CreatePartialSegmentDesc(dmar, rec);
            }
            if (status != ZX_OK) {
              zxlogf(ERROR, "acpi-bus: Failed to create iommu desc: %d", status);
              return status;
            }

            iommu_idx++;
            break;
          }
          case ACPI_DMAR_TYPE_RESERVED_MEMORY: {
            ACPI_DMAR_RESERVED_MEMORY* rec = (ACPI_DMAR_RESERVED_MEMORY*)record_hdr;
            zxlogf(DEBUG1, "DMAR Reserved Memory: %u %#llx %#llx", rec->Segment, rec->BaseAddress,
                   rec->EndAddress);
            uintptr_t addr = reinterpret_cast<uintptr_t>(record_hdr);
            for (uintptr_t scope = addr + 24; scope < addr + rec->Header.Length;) {
              ACPI_DMAR_DEVICE_SCOPE* s = (ACPI_DMAR_DEVICE_SCOPE*)scope;
              zxlogf(DEBUG1, "  DMAR Scope: %u, bus %u", s->EntryType, s->Bus);
              for (size_t i = 0; i < (s->Length - sizeof(*s)) / 2; ++i) {
                uint16_t v = *(uint16_t*)(scope + sizeof(*s) + 2 * i);
                zxlogf(DEBUG1, "    Path %ld: %02x.%02x", i, v & 0xffu, (uint16_t)(v >> 8));
              }
              scope += s->Length;
            }
            break;
          }
        }
        return ZX_ERR_NEXT;
      });

  if (iommu_idx != num_iommus) {
    zxlogf(ERROR, "%s: wrong number of IOMMUs found", __func__);
    return ZX_ERR_INTERNAL;
  }

  iommus_ = std::move(iommus);
  return ZX_OK;
}

zx::unowned_iommu IommuManager::IommuForBdf(uint32_t bdf) {
  fbl::AutoLock guard{&lock_};

  uint8_t bus = (uint8_t)(bdf >> 8);
  uint8_t dev_func = (uint8_t)bdf;

  for (auto& iommu : iommus_) {
    // TODO(teisenbe): Check segments in this function, once we support segments
    if (iommu.Desc().pci_segment != 0) {
      continue;
    }

    bool found_matching_scope = false;
    for (auto& scope : iommu.Scopes()) {
      // TODO(teisenbe): Once we support scopes with multiple hops, need to correct
      // this routine.
      // TODO(teisenbe): Once we support bridge entries, need to correct this routine.
      ZX_DEBUG_ASSERT(scope.num_hops == 1);
      if (scope.start_bus != bus) {
        continue;
      }
      if (scope.dev_func[0] == dev_func) {
        found_matching_scope = true;
        break;
      }
    }

    // Finding a scope has its meaning inverted based on whether this device is whole segment mode.
    if (iommu.Desc().whole_segment != found_matching_scope) {
      return iommu.GetIommu();
    }
  }

  // If there was no match, just use the dummy.
  return zx::unowned_iommu(dummy_iommu_);
}

}  // namespace x86

zx_status_t iommu_manager_iommu_for_bdf(uint32_t bdf, zx_handle_t* iommu) {
  ZX_DEBUG_ASSERT(iommu_mgr);
  *iommu = iommu_mgr->IommuForBdf(bdf)->get();
  return ZX_OK;
}
