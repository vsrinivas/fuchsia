// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __PLATFORM_ACPI_H
#define __PLATFORM_ACPI_H

#include <compiler.h>

#include <acpica/acpi.h>

#include <arch/x86/apic.h>
#include <dev/pcie.h>

__BEGIN_CDECLS

struct acpi_pcie_config {
    // Physical address of the configuration space window for bus start_bus
    // NOTE: The ACPI deata actually yields the address for bus 0 always, so
    // we adjust it before populating this structure.
    paddr_t ecam_phys;
    size_t ecam_size;
    // ACPI segment group
    uint16_t segment_group;
    // The range of buses supported by this bridge
    uint8_t start_bus;
    uint8_t end_bus;
};

struct acpi_irq_signal {
    uint32_t global_irq;
    bool level_triggered;
    bool active_high;
};

#define ACPI_NO_IRQ_MAPPING UINT32_MAX
#define MAX_IRQ_SIGNALS 16
struct acpi_pcie_irq_mapping {
    uint32_t dev_pin_to_global_irq[PCIE_MAX_DEVICES_PER_BUS][PCIE_MAX_FUNCTIONS_PER_DEVICE][PCIE_MAX_LEGACY_IRQ_PINS];

    uint32_t num_irqs;
    struct acpi_irq_signal irqs[MAX_IRQ_SIGNALS];
};

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
status_t platform_find_pcie_config(struct acpi_pcie_config *config);
status_t platform_find_pcie_legacy_irq_mapping(struct acpi_pcie_irq_mapping *root_bus_map);
status_t platform_find_hpet(struct acpi_hpet_descriptor *hpet);

// Powers off the machine.  Returns on failure
void acpi_poweroff(void);
// Reboots the machine.  Returns on failure
void acpi_reboot(void);

#define POWER_BUTTON_PORT "sys/pwr/sw"

__END_CDECLS

#endif
