// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/compiler.h>
#include <magenta/syscalls/object.h>
#include <stdint.h>

__BEGIN_CDECLS

// resource record types
#define MX_RREC_DELETED  (0)
#define MX_RREC_SELF     (1)
#define MX_RREC_MMIO     (2)
#define MX_RREC_IRQ      (3)

// resource subtypes for SELF
#define MX_RREC_SELF_GENERIC  (0) // no special purpose
#define MX_RREC_SELF_ROOT     (1) // root of kernel resource tree

// The 0th record of every resource is of type MX_RREC_SELF and describes
// the resource itself.  Resources that are not simply access tokens
// or “directories” of other resources will have additional records.
typedef struct mx_rrec_self {
    uint16_t type;              // MX_RREC_SELF
    uint16_t subtype;
    uint32_t options;
    mx_koid_t koid;             // kernel object id of this resource
    uint32_t record_count;      // count of records in this resource
    uint32_t child_count;       // count of children of this resource
    uint32_t reserved[2];
    char name[MX_MAX_NAME_LEN]; // human readable name of this resource
} mx_rrec_self_t;

// Memory Mapped IO Regions
typedef struct mx_rrec_mmio {
    uint16_t type;              // MX_RREC_MMIO
    uint16_t subtype;
    uint32_t options;
    uint64_t phys_base;         // physical base address
    uint64_t phys_size;         // size of mmio aperture
    uint32_t reserved[10];
} mx_rrec_mmio_t;

// IRQs
typedef struct mx_rrec_irq {
    uint16_t type;             // MX_RREC_IRQ
    uint16_t subtype;
    uint32_t options;
    uint32_t irq_base;         // hw irq number, if such exists
    uint32_t irq_count;        // number of irqs
    uint32_t reserved[12];
} mx_rrec_irq_t;

typedef union mx_rrec {
    uint16_t type;
    mx_rrec_self_t self;
    mx_rrec_mmio_t mmio;
    mx_rrec_irq_t irq;
    uint8_t raw[64];
} mx_rrec_t;

static_assert(sizeof(mx_rrec_t) == 64, "OOPS");

__END_CDECLS
