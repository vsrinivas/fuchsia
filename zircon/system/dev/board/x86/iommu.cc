// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iommu.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/syscalls/iommu.h>

#include <acpica/acpi.h>
#include <ddk/debug.h>

typedef struct iommu_info {
  const zx_iommu_desc_intel_t* desc;  // owned by this structure
  size_t desc_len;

  zx_handle_t handle;  // ZX_HANDLE_INVALID if not activated
} iommu_info_t;

typedef struct {
  mtx_t lock;
  iommu_info_t* iommus;  // Array of IOMMUs
  size_t num_iommus;     // Length of |iommus|

  zx_handle_t dummy_iommu;  // Used for BDFs not covered by the ACPI tables.
} iommu_manager_t;

static iommu_manager_t iommu_mgr;

static zx_status_t acpi_scope_to_desc(ACPI_DMAR_DEVICE_SCOPE* acpi_scope,
                                      zx_iommu_desc_intel_scope_t* desc_scope) {
  switch (acpi_scope->EntryType) {
    case ACPI_DMAR_SCOPE_TYPE_ENDPOINT:
      desc_scope->type = ZX_IOMMU_INTEL_SCOPE_ENDPOINT;
      break;
    case ACPI_DMAR_SCOPE_TYPE_BRIDGE:
      zxlogf(INFO, "acpi-bus: bridge scopes not supported\n");
      return ZX_ERR_NOT_SUPPORTED;
    default:
      // Skip this scope, since it's not a type we care about.
      return ZX_ERR_WRONG_TYPE;
  }
  desc_scope->start_bus = acpi_scope->Bus;
  if (acpi_scope->Length < sizeof(*acpi_scope)) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  desc_scope->num_hops = static_cast<uint8_t>((acpi_scope->Length - sizeof(*acpi_scope)) / 2);
  if (countof(desc_scope->dev_func) < desc_scope->num_hops) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  // TODO(teisenbe): We need to be aware of the mapping between
  // PCI paths and bus numbers to properly evaluate this.
  if (desc_scope->num_hops != 1) {
    zxlogf(INFO, "acpi-bus: non root bus devices not supported\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Walk the variable-length array of hops that is appended to the main
  // ACPI_DMAR_DEVICE_SCOPE structure.
  for (ssize_t i = 0; i < desc_scope->num_hops; ++i) {
    uint16_t v = *(uint16_t*)((uintptr_t)acpi_scope + sizeof(*acpi_scope) + 2 * i);
    const uint8_t dev = v & 0x1f;
    const uint8_t func = (v >> 8) & 0x7;
    desc_scope->dev_func[i] = static_cast<uint8_t>((dev << 3) | func);
  }
  return ZX_OK;
}

// Walks the given unit's scopes and appends them to the given descriptor.
// |max_scopes| is the number of scopes |scopes| can hold. |num_scopes_found|
// is the number of scopes found on |unit|, even if they wouldn't all fit in |scopes|.
static zx_status_t append_scopes(ACPI_DMAR_HARDWARE_UNIT* unit, size_t max_scopes,
                                 zx_iommu_desc_intel_scope_t* scopes, size_t* num_scopes_found) {
  size_t num_scopes = 0;
  uintptr_t scope;
  const uintptr_t addr = (uintptr_t)unit;
  for (scope = addr + 16; scope < addr + unit->Header.Length;) {
    ACPI_DMAR_DEVICE_SCOPE* s = (ACPI_DMAR_DEVICE_SCOPE*)scope;
    zxlogf(DEBUG1, "  DMAR Scope: %u, bus %u\n", s->EntryType, s->Bus);
    for (size_t i = 0; i < (s->Length - sizeof(*s)) / 2; ++i) {
      uint16_t v = *(uint16_t*)(scope + sizeof(*s) + 2 * i);
      zxlogf(DEBUG1, "    Path %ld: %02x.%02x\n", i, v & 0xffu, (uint16_t)(v >> 8));
    }
    scope += s->Length;

    // Count the scopes we care about
    switch (s->EntryType) {
      case ACPI_DMAR_SCOPE_TYPE_ENDPOINT:
      case ACPI_DMAR_SCOPE_TYPE_BRIDGE:
        num_scopes++;
        break;
    }
  }

  if (num_scopes_found) {
    *num_scopes_found = num_scopes;
  }
  if (!scopes) {
    return ZX_OK;
  }

  if (num_scopes > max_scopes) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  size_t cur_num_scopes = 0;
  for (scope = addr + 16; scope < addr + unit->Header.Length && cur_num_scopes < max_scopes;) {
    ACPI_DMAR_DEVICE_SCOPE* s = (ACPI_DMAR_DEVICE_SCOPE*)scope;

    zx_status_t status = acpi_scope_to_desc(s, &scopes[cur_num_scopes]);
    if (status != ZX_OK && status != ZX_ERR_WRONG_TYPE) {
      return status;
    }
    if (status == ZX_OK) {
      cur_num_scopes++;
    }

    scope += s->Length;
  }

  // Since |num_scopes| is the number of ENDPOINT and BRIDGE entries, and
  // |acpi_scope_to_desc| doesn't return ZX_ERR_WRONG_TYPE for those types of
  // entries, we should always have seen that number of entries when we reach
  // here.
  assert(cur_num_scopes == num_scopes);
  return ZX_OK;
}

static bool scope_eq(zx_iommu_desc_intel_scope_t* scope, ACPI_DMAR_DEVICE_SCOPE* acpi_scope) {
  zx_iommu_desc_intel_scope_t other_scope;
  zx_status_t status = acpi_scope_to_desc(acpi_scope, &other_scope);
  if (status != ZX_OK) {
    return false;
  }

  if (scope->type != other_scope.type || scope->start_bus != other_scope.start_bus ||
      scope->num_hops != other_scope.num_hops) {
    return false;
  }

  for (size_t i = 0; i < scope->num_hops; ++i) {
    if (scope->dev_func[i] != other_scope.dev_func[i]) {
      return false;
    }
  }

  return true;
}

// Appends to desc any reserved memory regions that match its scopes. If
// |desc_len| is not large enough to include the reserved memory descriptors, this
// function will not append all of the found entries. |bytes_needed| will
// always return the number of bytes needed to represent all of the reserved
// descriptors. This function does not modify desc->reserved_mem_bytes.
static zx_status_t append_reserved_mem(ACPI_TABLE_DMAR* table, zx_iommu_desc_intel_t* desc,
                                       size_t desc_len, size_t* bytes_needed) {
  const uintptr_t records_start = (uintptr_t)table + sizeof(*table);
  const uintptr_t records_end = (uintptr_t)table + table->Header.Length;

  zx_iommu_desc_intel_scope_t* desc_scopes =
      (zx_iommu_desc_intel_scope_t*)((uintptr_t)desc + sizeof(*desc));
  const size_t num_desc_scopes = desc->scope_bytes / sizeof(zx_iommu_desc_intel_scope_t);

  uintptr_t next_reserved_mem_desc_base = (uintptr_t)desc + sizeof(zx_iommu_desc_intel_t) +
                                          desc->scope_bytes + desc->reserved_memory_bytes;

  *bytes_needed = 0;
  for (uintptr_t addr = records_start; addr < records_end;) {
    ACPI_DMAR_HEADER* record_hdr = (ACPI_DMAR_HEADER*)addr;
    switch (record_hdr->Type) {
      case ACPI_DMAR_TYPE_RESERVED_MEMORY: {
        ACPI_DMAR_RESERVED_MEMORY* rec = (ACPI_DMAR_RESERVED_MEMORY*)record_hdr;

        if (desc->pci_segment != rec->Segment) {
          break;
        }

        zx_iommu_desc_intel_reserved_memory_t* mem_desc =
            (zx_iommu_desc_intel_reserved_memory_t*)next_reserved_mem_desc_base;
        size_t mem_desc_size = sizeof(*mem_desc);

        // Search for scopes that match
        for (uintptr_t scope = addr + 24; scope < addr + rec->Header.Length;) {
          ACPI_DMAR_DEVICE_SCOPE* s = (ACPI_DMAR_DEVICE_SCOPE*)scope;
          // TODO(teisenbe): We should skip scope types we don't
          // care about here

          // Search for a scope in the descriptor that matches this
          // ACPI scope.
          bool no_matches = true;
          for (size_t i = 0; i < num_desc_scopes; ++i) {
            zx_iommu_desc_intel_scope_t* scope_desc = &desc_scopes[i];
            const bool scope_matches = scope_eq(scope_desc, s);

            no_matches &= !scope_matches;

            // If this is a whole segment descriptor, then a match
            // corresponds to an entry we should ignore.
            if (scope_matches && !desc->whole_segment) {
              zx_iommu_desc_intel_scope_t* new_scope_desc =
                  (zx_iommu_desc_intel_scope_t*)(next_reserved_mem_desc_base + mem_desc_size);
              mem_desc_size += sizeof(zx_iommu_desc_intel_scope_t);

              if (next_reserved_mem_desc_base + mem_desc_size <= (uintptr_t)desc + desc_len) {
                memcpy(new_scope_desc, scope_desc, sizeof(*scope_desc));
              }
              break;
            }
          }

          if (no_matches && desc->whole_segment) {
            zx_iommu_desc_intel_scope_t other_scope;
            zx_status_t status = acpi_scope_to_desc(s, &other_scope);
            if (status != ZX_ERR_WRONG_TYPE && status != ZX_OK) {
              return status;
            }
            if (status == ZX_OK) {
              zx_iommu_desc_intel_scope_t* new_scope_desc =
                  (zx_iommu_desc_intel_scope_t*)(next_reserved_mem_desc_base + mem_desc_size);
              mem_desc_size += sizeof(zx_iommu_desc_intel_scope_t);

              if (next_reserved_mem_desc_base + mem_desc_size <= (uintptr_t)desc + desc_len) {
                memcpy(new_scope_desc, &other_scope, sizeof(other_scope));
              }
            }
          }

          scope += s->Length;
        }

        // If this descriptor does not have any scopes, ignore it
        if (mem_desc_size == sizeof(*mem_desc)) {
          break;
        }

        if (next_reserved_mem_desc_base + mem_desc_size <= (uintptr_t)desc + desc_len) {
          mem_desc->base_addr = rec->BaseAddress;
          mem_desc->len = rec->EndAddress - rec->BaseAddress + 1;
          mem_desc->scope_bytes = static_cast<uint8_t>(mem_desc_size - sizeof(*mem_desc));
          next_reserved_mem_desc_base += mem_desc_size;
        }
        *bytes_needed += mem_desc_size;

        break;
      }
    }

    addr += record_hdr->Length;
  }

  // Check if we weren't able to write all of the entries above.
  if (*bytes_needed + sizeof(zx_iommu_desc_intel_t) + desc->scope_bytes +
          desc->reserved_memory_bytes >
      desc_len) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  return ZX_OK;
}

static zx_status_t create_whole_segment_iommu_desc(ACPI_TABLE_DMAR* table,
                                                   ACPI_DMAR_HARDWARE_UNIT* unit,
                                                   zx_iommu_desc_intel_t** desc_out,
                                                   size_t* desc_len_out) {
  assert(unit->Flags & ACPI_DMAR_INCLUDE_ALL);

  // The VT-d spec requires that whole-segment hardware units appear in the
  // DMAR table after all other hardware units on their segment. Search those
  // entries for scopes to specify as excluded from this descriptor.

  size_t num_scopes = 0;
  size_t num_scopes_on_unit;

  const uintptr_t records_start = ((uintptr_t)table) + sizeof(*table);
  const uintptr_t records_end = (uintptr_t)unit + unit->Header.Length;

  uintptr_t addr;
  for (addr = records_start; addr < records_end;) {
    ACPI_DMAR_HEADER* record_hdr = (ACPI_DMAR_HEADER*)addr;
    switch (record_hdr->Type) {
      case ACPI_DMAR_TYPE_HARDWARE_UNIT: {
        ACPI_DMAR_HARDWARE_UNIT* rec = (ACPI_DMAR_HARDWARE_UNIT*)record_hdr;
        if (rec->Segment != unit->Segment) {
          break;
        }
        zx_status_t status = append_scopes(rec, 0, NULL, &num_scopes_on_unit);
        if (status != ZX_OK) {
          return status;
        }
        num_scopes += num_scopes_on_unit;
      }
    }
    addr += record_hdr->Length;
  }

  size_t desc_len =
      sizeof(zx_iommu_desc_intel_t) + sizeof(zx_iommu_desc_intel_scope_t) * num_scopes;
  zx_iommu_desc_intel_t* desc = static_cast<zx_iommu_desc_intel_t*>(malloc(desc_len));
  if (!desc) {
    return ZX_ERR_NO_MEMORY;
  }
  desc->register_base = unit->Address;
  desc->pci_segment = unit->Segment;
  desc->whole_segment = true;
  desc->scope_bytes = 0;
  desc->reserved_memory_bytes = 0;

  for (addr = records_start; addr < records_end;) {
    ACPI_DMAR_HEADER* record_hdr = (ACPI_DMAR_HEADER*)addr;
    switch (record_hdr->Type) {
      case ACPI_DMAR_TYPE_HARDWARE_UNIT: {
        ACPI_DMAR_HARDWARE_UNIT* rec = (ACPI_DMAR_HARDWARE_UNIT*)record_hdr;
        if (rec->Segment != unit->Segment) {
          break;
        }
        size_t scopes_found = 0;
        zx_iommu_desc_intel_scope_t* scopes =
            (zx_iommu_desc_intel_scope_t*)((uintptr_t)desc + sizeof(*desc) + desc->scope_bytes);
        zx_status_t status = append_scopes(rec, num_scopes, scopes, &scopes_found);
        if (status != ZX_OK) {
          free(desc);
          return status;
        }
        desc->scope_bytes = static_cast<uint8_t>(
            desc->scope_bytes + scopes_found * sizeof(zx_iommu_desc_intel_scope_t));
        num_scopes -= scopes_found;
      }
    }
    addr += record_hdr->Length;
  }

  size_t reserved_mem_bytes = 0;
  zx_status_t status = append_reserved_mem(table, desc, desc_len, &reserved_mem_bytes);
  if (status == ZX_ERR_BUFFER_TOO_SMALL) {
    zx_iommu_desc_intel_t* new_desc =
        static_cast<zx_iommu_desc_intel_t*>(realloc(desc, desc_len + reserved_mem_bytes));
    if (new_desc == NULL) {
      free(desc);
      return ZX_ERR_NO_MEMORY;
    }
    desc = new_desc;
    desc_len += reserved_mem_bytes;
    status = append_reserved_mem(table, desc, desc_len, &reserved_mem_bytes);
  }
  if (status != ZX_OK) {
    free(desc);
    return status;
  }
  desc->reserved_memory_bytes =
      static_cast<uint16_t>(desc->reserved_memory_bytes + reserved_mem_bytes);

  *desc_out = desc;
  *desc_len_out = desc_len;
  return ZX_OK;
}

static zx_status_t create_partial_segment_iommu_desc(ACPI_TABLE_DMAR* table,
                                                     ACPI_DMAR_HARDWARE_UNIT* unit,
                                                     zx_iommu_desc_intel_t** desc_out,
                                                     size_t* desc_len_out) {
  assert((unit->Flags & ACPI_DMAR_INCLUDE_ALL) == 0);

  size_t num_scopes;
  zx_status_t status = append_scopes(unit, 0, NULL, &num_scopes);
  if (status != ZX_OK) {
    return status;
  }

  size_t desc_len =
      sizeof(zx_iommu_desc_intel_t) + sizeof(zx_iommu_desc_intel_scope_t) * num_scopes;
  zx_iommu_desc_intel_t* desc = static_cast<zx_iommu_desc_intel_t*>(malloc(desc_len));
  if (!desc) {
    return ZX_ERR_NO_MEMORY;
  }
  desc->register_base = unit->Address;
  desc->pci_segment = unit->Segment;
  desc->whole_segment = false;
  desc->scope_bytes = 0;
  desc->reserved_memory_bytes = 0;
  zx_iommu_desc_intel_scope_t* scopes =
      (zx_iommu_desc_intel_scope_t*)((uintptr_t)desc + sizeof(*desc));
  size_t actual_num_scopes;
  status = append_scopes(unit, num_scopes, scopes, &actual_num_scopes);
  if (status != ZX_OK) {
    free(desc);
    return status;
  }
  desc->scope_bytes = static_cast<uint8_t>(actual_num_scopes * sizeof(zx_iommu_desc_intel_scope_t));

  size_t reserved_mem_bytes = 0;
  status = append_reserved_mem(table, desc, desc_len, &reserved_mem_bytes);
  if (status == ZX_ERR_BUFFER_TOO_SMALL) {
    zx_iommu_desc_intel_t* new_desc =
        static_cast<zx_iommu_desc_intel_t*>(realloc(desc, desc_len + reserved_mem_bytes));
    if (new_desc == NULL) {
      status = ZX_ERR_NO_MEMORY;
      goto cleanup;
    }
    desc = new_desc;
    desc_len += reserved_mem_bytes;
    status = append_reserved_mem(table, desc, desc_len, &reserved_mem_bytes);
  }
  if (status != ZX_OK) {
    goto cleanup;
  }
  desc->reserved_memory_bytes =
      static_cast<uint16_t>(desc->reserved_memory_bytes + reserved_mem_bytes);

  *desc_out = desc;
  *desc_len_out = desc_len;
  return ZX_OK;
cleanup:
  free(desc);
  return status;
}

static bool use_hardware_iommu(void) {
  const char* value = getenv("driver.iommu.enable");
  if (value == NULL) {
    return false;  // Default to false currently
  } else if (!strcmp(value, "0") || !strcmp(value, "false") || !strcmp(value, "off")) {
    return false;
  } else {
    return true;
  }
}

zx_status_t iommu_manager_init(void) {
  int err = mtx_init(&iommu_mgr.lock, mtx_plain);
  if (err != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  iommu_mgr.iommus = NULL;
  iommu_mgr.num_iommus = 0;

  zx_iommu_desc_dummy_t dummy;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx_status_t status = zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &dummy,
                                       sizeof(dummy), &iommu_mgr.dummy_iommu);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi-bus: error %d in zx_iommu_create for dummy\n", status);
    return status;
  }

  if (!use_hardware_iommu()) {
    zxlogf(INFO, "acpi-bus: not using IOMMU\n");
    return ZX_OK;
  }

  ACPI_TABLE_HEADER* table = NULL;
  ACPI_STATUS acpi_status = AcpiGetTable((char*)ACPI_SIG_DMAR, 1, &table);
  if (acpi_status != AE_OK) {
    zxlogf(INFO, "acpi-bus: could not find DMAR table\n");
    return ZX_ERR_NOT_FOUND;
  }
  ACPI_TABLE_DMAR* dmar = (ACPI_TABLE_DMAR*)table;
  const uintptr_t records_start = ((uintptr_t)dmar) + sizeof(*dmar);
  const uintptr_t records_end = ((uintptr_t)dmar) + dmar->Header.Length;
  if (records_start >= records_end) {
    zxlogf(ERROR, "acpi-bus: DMAR wraps around address space\n");
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  // Shouldn't be too many records
  if (dmar->Header.Length > 4096) {
    zxlogf(ERROR, "acpi-bus: DMAR suspiciously long: %u\n", dmar->Header.Length);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Count the IOMMUs
  size_t num_iommus = 0;
  uintptr_t addr;
  for (addr = records_start; addr < records_end;) {
    ACPI_DMAR_HEADER* record_hdr = (ACPI_DMAR_HEADER*)addr;
    if (record_hdr->Type == ACPI_DMAR_TYPE_HARDWARE_UNIT) {
      num_iommus++;
    }

    addr += record_hdr->Length;
  }
  if (addr != records_end) {
    zxlogf(ERROR, "acpi-bus: DMAR length weird: %u, reached %zu\n", dmar->Header.Length,
           records_end - records_start);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  if (num_iommus == 0) {
    return ZX_OK;
  }

  iommu_mgr.iommus = static_cast<iommu_info_t*>(malloc(sizeof(iommu_info_t) * num_iommus));
  if (iommu_mgr.iommus == NULL) {
    return ZX_ERR_NO_MEMORY;
  }

  for (addr = records_start; addr < records_end;) {
    ACPI_DMAR_HEADER* record_hdr = (ACPI_DMAR_HEADER*)addr;
    zxlogf(DEBUG1, "DMAR record: %d\n", record_hdr->Type);
    switch (record_hdr->Type) {
      case ACPI_DMAR_TYPE_HARDWARE_UNIT: {
        ACPI_DMAR_HARDWARE_UNIT* rec = (ACPI_DMAR_HARDWARE_UNIT*)record_hdr;

        zxlogf(DEBUG1, "DMAR Hardware Unit: %u %#llx %#x\n", rec->Segment, rec->Address,
               rec->Flags);
        const bool whole_segment = rec->Flags & ACPI_DMAR_INCLUDE_ALL;

        zx_iommu_desc_intel_t* desc = NULL;
        size_t desc_len = 0;
        if (whole_segment) {
          status = create_whole_segment_iommu_desc(dmar, rec, &desc, &desc_len);
        } else {
          status = create_partial_segment_iommu_desc(dmar, rec, &desc, &desc_len);
        }
        if (status != ZX_OK) {
          zxlogf(ERROR, "acpi-bus: Failed to create iommu desc: %d\n", status);
          goto cleanup;
        }

        zx_handle_t iommu_handle;
        // Please do not use get_root_resource() in new code. See ZX-1467.
        status = zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_INTEL, desc, desc_len,
                                 &iommu_handle);
        if (status != ZX_OK) {
          zxlogf(ERROR, "acpi-bus: Failed to create iommu object: %d\n", status);
          goto cleanup;
        }

        ZX_DEBUG_ASSERT(iommu_mgr.num_iommus < num_iommus);
        size_t idx = iommu_mgr.num_iommus;
        iommu_mgr.iommus[idx].desc = desc;
        iommu_mgr.iommus[idx].desc_len = desc_len;
        iommu_mgr.iommus[idx].handle = iommu_handle;
        iommu_mgr.num_iommus++;
        break;
      }
      case ACPI_DMAR_TYPE_RESERVED_MEMORY: {
        ACPI_DMAR_RESERVED_MEMORY* rec = (ACPI_DMAR_RESERVED_MEMORY*)record_hdr;
        zxlogf(DEBUG1, "DMAR Reserved Memory: %u %#llx %#llx\n", rec->Segment, rec->BaseAddress,
               rec->EndAddress);
        for (uintptr_t scope = addr + 24; scope < addr + rec->Header.Length;) {
          ACPI_DMAR_DEVICE_SCOPE* s = (ACPI_DMAR_DEVICE_SCOPE*)scope;
          zxlogf(DEBUG1, "  DMAR Scope: %u, bus %u\n", s->EntryType, s->Bus);
          for (size_t i = 0; i < (s->Length - sizeof(*s)) / 2; ++i) {
            uint16_t v = *(uint16_t*)(scope + sizeof(*s) + 2 * i);
            zxlogf(DEBUG1, "    Path %ld: %02x.%02x\n", i, v & 0xffu, (uint16_t)(v >> 8));
          }
          scope += s->Length;
        }
        break;
      }
    }

    addr += record_hdr->Length;
  }

  zxlogf(INFO, "acpi-bus: using IOMMU\n");
  return ZX_OK;
cleanup:
  for (size_t i = 0; i < iommu_mgr.num_iommus; ++i) {
    zx_handle_close(iommu_mgr.iommus[i].handle);
    free((void*)iommu_mgr.iommus[i].desc);
  }
  free(iommu_mgr.iommus);
  iommu_mgr.iommus = NULL;
  iommu_mgr.num_iommus = 0;
  return status;
}

zx_status_t iommu_manager_iommu_for_bdf(uint32_t bdf, zx_handle_t* iommu_h) {
  mtx_lock(&iommu_mgr.lock);

  uint8_t bus = (uint8_t)(bdf >> 8);
  uint8_t dev_func = (uint8_t)bdf;

  iommu_info_t* match = NULL;
  for (size_t i = 0; i < iommu_mgr.num_iommus; ++i) {
    iommu_info_t* iommu = &iommu_mgr.iommus[i];
    const zx_iommu_desc_intel_t* desc = iommu->desc;

    // TODO(teisenbe): Check segments in this function, once we support segments
    if (desc->pci_segment != 0) {
      continue;
    }

    const uintptr_t scope_base = (uintptr_t)desc + sizeof(zx_iommu_desc_intel_t);
    const zx_iommu_desc_intel_scope_t* scopes = (const zx_iommu_desc_intel_scope_t*)scope_base;
    const size_t num_scopes = desc->scope_bytes / sizeof(scopes[0]);

    bool found_matching_scope = false;
    for (size_t i = 0; i < num_scopes; ++i) {
      // TODO(teisenbe): Once we support scopes with multiple hops, need to correct
      // this routine.
      // TODO(teisenbe): Once we support bridge entries, need to correct this routine.
      ZX_DEBUG_ASSERT(scopes[i].num_hops == 1);
      if (scopes[i].start_bus != bus) {
        continue;
      }
      if (scopes[i].dev_func[0] == dev_func) {
        found_matching_scope = true;
        break;
      }
    }

    if (desc->whole_segment) {
      // If we're in whole-segment mode, a match in the scope list means
      // this device is *not* valid for this BDF.
      if (!found_matching_scope) {
        match = iommu;
        break;
      }
    } else {
      // If we're not in whole-segment mode, a match in the scope list
      // means this device is valid for this BDF.
      if (found_matching_scope) {
        match = iommu;
        break;
      }
    }
  }

  if (match) {
    *iommu_h = match->handle;
  } else {
    // If there was no match, just use the dummy handle
    *iommu_h = iommu_mgr.dummy_iommu;
  }

  mtx_unlock(&iommu_mgr.lock);
  return ZX_OK;
}

zx_status_t iommu_manager_get_dummy_iommu(zx_handle_t* iommu) {
  *iommu = iommu_mgr.dummy_iommu;
  return ZX_OK;
}
