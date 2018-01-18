// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

__BEGIN_CDECLS;
void gicv2_hw_interface_register(void);
__END_CDECLS

// Representation of GICH registers.
typedef struct Gich_ {
    uint32_t hcr;
    uint32_t vtr;
    uint32_t vmcr;
    uint32_t reserved0;
    uint32_t misr;
    uint32_t reserved1[3];
    uint64_t eisr;
    uint32_t reserved2[2];
    uint64_t elrs;
    uint32_t reserved3[46];
    uint32_t apr;
    uint32_t reserved4[3];
    uint32_t lr[64];
} __attribute__((__packed__)) Gich;

static_assert(__offsetof(Gich, hcr) == 0x00, "");
static_assert(__offsetof(Gich, vtr) == 0x04, "");
static_assert(__offsetof(Gich, vmcr) == 0x08, "");
static_assert(__offsetof(Gich, misr) == 0x10, "");
static_assert(__offsetof(Gich, eisr) == 0x20, "");
static_assert(__offsetof(Gich, elrs) == 0x30, "");
static_assert(__offsetof(Gich, apr) == 0xf0, "");
static_assert(__offsetof(Gich, lr) == 0x100, "");
