// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/x86/feature.h>
#include <zircon/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS

typedef struct {
    uint32_t package_id;
    uint32_t node_id;
    uint32_t core_id;
    uint32_t smt_id;
} x86_cpu_topology_t;

void x86_cpu_topology_init(void);
void x86_cpu_topology_decode(uint32_t apic_id, x86_cpu_topology_t *topo);

__END_CDECLS
