// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_SYSCALLS_IOMMU_H_
#define SYSROOT_ZIRCON_SYSCALLS_IOMMU_H_

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

#define ZX_IOMMU_MAX_DESC_LEN 4096

// Values for the |type| argument of the zx_iommu_create() syscall.
#define ZX_IOMMU_TYPE_DUMMY 0
#define ZX_IOMMU_TYPE_INTEL 1

// Data structures for creating a dummy IOMMU instance
typedef struct zx_iommu_desc_dummy {
  uint8_t reserved;
} zx_iommu_desc_dummy_t;

// Data structures for creating an Intel IOMMU instance

// This scope represents a single PCI endpoint device
#define ZX_IOMMU_INTEL_SCOPE_ENDPOINT 0
// This scope represents a PCI-PCI bridge.  The bridge and all of its downstream
// devices will be included in this scope.
#define ZX_IOMMU_INTEL_SCOPE_BRIDGE 1

// TODO(teisenbe): Investigate FIDL for this.  Multiple embedded lists seems
// right up its alley.
typedef struct zx_iommu_desc_intel_scope {
  uint8_t type;
  // The bus number of the first bus decoded by the host bridge this scope is attached to.
  uint8_t start_bus;
  // Number of bridges (including the host bridge) between host bridge and the
  // device.
  uint8_t num_hops;
  // The device number and function numbers of the bridges along the way,
  // ending with the device itself.
  // |dev_func[0]| is the address on |start_bus| of the first bridge in the
  // path (excluding the host bridge).  |dev_func[num_hops-1]| is the address
  // of the device itself.
  uint8_t dev_func[5];
} zx_iommu_desc_intel_scope_t;

typedef struct zx_iommu_desc_intel_reserved_memory {
  uint64_t base_addr;  // Physical address of the base of reserved memory.
  uint64_t len;        // Number of bytes of reserved memory.

  // The number of bytes of zx_iommu_desc_intel_scope_t's that follow this descriptor.
  uint8_t scope_bytes;

  uint8_t _reserved[7];  // Padding

  // This is a list of all devices that need access to this memory range.
  //
  // zx_iommu_desc_intel_scope_t scopes[num_scopes];
} zx_iommu_desc_intel_reserved_memory_t;

typedef struct zx_iommu_desc_intel {
  uint64_t register_base;  // Physical address of registers
  uint16_t pci_segment;    // The PCI segment associated with this IOMMU

  // If false, scopes[] represents all PCI devices in this segment managed by this IOMMU.
  // If true, scopes[] represents all PCI devices in this segment *not* managed by this IOMMU.
  bool whole_segment;

  // The number of bytes of zx_iommu_desc_intel_scope_t's that follow this descriptor.
  uint8_t scope_bytes;

  // The number of bytes of zx_iommu_desc_intel_reserved_memory_t's that follow the scope
  // list.
  uint16_t reserved_memory_bytes;

  uint8_t _reserved[2];  // Padding

  // If |whole_segment| is false, this is a list of all devices managed by
  // this IOMMU.  If |whole_segment| is true, this is a list of all devices on
  // this segment *not* managed by this IOMMU.  It has a total length in bytes of
  // |scope_bytes|.
  //
  // zx_iommu_desc_intel_scope_t scopes[];

  // A list of all BIOS-reserved memory regions this IOMMU needs to translate.
  // It has a total length in bytes of |reserved_memory_bytes|.
  //
  // zx_iommu_desc_intel_reserved_memory_t reserved_mem[];
} zx_iommu_desc_intel_t;

__END_CDECLS

#endif  // SYSROOT_ZIRCON_SYSCALLS_IOMMU_H_
