// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <acpica/acpi.h>
#include <arch/x86/apic.h>

__BEGIN_CDECLS

struct acpi_hpet_descriptor {
    uint64_t address;
    bool port_io;

    uint16_t minimum_tick;
    uint8_t sequence;
};

void platform_init_acpi_tables(uint levels);
void platform_init_acpi(void);
status_t platform_enumerate_cpus(
        uint32_t *apic_ids,
        uint32_t len,
        uint32_t *num_cpus);
status_t platform_enumerate_io_apics(
        struct io_apic_descriptor *io_apics,
        uint32_t len,
        uint32_t *num_io_apics);
status_t platform_enumerate_interrupt_source_overrides(
        struct io_apic_isa_override *isos,
        uint32_t len,
        uint32_t *num_isos);
status_t platform_find_hpet(struct acpi_hpet_descriptor *hpet);

__END_CDECLS
