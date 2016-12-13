// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <magenta/compiler.h>
#include <magenta/syscalls/object.h>
#include <stdint.h>

__BEGIN_CDECLS

// resource record types
#define MX_RREC_DELETED  (0u)
#define MX_RREC_SELF     (1u)
#define MX_RREC_DATA     (2u)
#define MX_RREC_IRQ      (3u)
#define MX_RREC_MMIO     (4u)
#define MX_RREC_IOPORT   (5u)

// actions
#define MX_RACT_ENABLE   (1u)
#define MX_RACT_DISABLE  (2u)

// resource subtypes for SELF
#define MX_RREC_SELF_GENERIC  (0u) // no special purpose
#define MX_RREC_SELF_ROOT     (1u) // root of kernel resource tree

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

// IO Ports
typedef struct mx_rrec_ioport {
    uint16_t type;             // MX_RREC_IOPORT
    uint16_t subtype;
    uint32_t options;
    uint32_t port_base;
    uint32_t port_count;
    uint32_t reserved[12];
} mx_rrec_ioport_t;

// resource subtypes for DATA
#define MX_RREC_DATA_U8     (1u)
#define MX_RREC_DATA_U32    (2u)
#define MX_RREC_DATA_U64    (3u)
#define MX_RREC_DATA_STRING (4u)

typedef struct mx_rrec_data {
    uint16_t type;
    uint16_t subtype;
    uint32_t options;         // low 4 bits are count
    union {
        uint32_t u32[14];
        uint64_t u64[7];
        uint8_t u8[56];
        char c[56];
    };
} mx_rrec_data_t;

typedef union mx_rrec {
    uint16_t type;
    mx_rrec_self_t self;
    mx_rrec_data_t data;
    mx_rrec_irq_t irq;
    mx_rrec_mmio_t mmio;
    mx_rrec_ioport_t ioport;
    uint8_t raw[64];
} mx_rrec_t;

static_assert(sizeof(mx_rrec_t) == 64, "OOPS");

__END_CDECLS
