// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <acpica/acpi.h>

#include <assert.h>
#include <err.h>
#include <trace.h>

#include <lk/init.h>

#include <arch/x86/apic.h>
#include <kernel/port.h>
#include <platform/acpi.h>

#define LOCAL_TRACE 0

#define ACPI_MAX_INIT_TABLES 16
static ACPI_TABLE_DESC acpi_tables[ACPI_MAX_INIT_TABLES];
static bool acpi_initialized = false;

static ACPI_STATUS acpi_set_apic_irq_mode(void);

/**
 * @brief  Initialize early-access ACPI tables
 *
 * This function enables *only* the ACPICA Table Manager subsystem.
 * The rest of the ACPI subsystem will remain uninitialized.
 */
void platform_init_acpi_tables(uint level)
{
    DEBUG_ASSERT(!acpi_initialized);

    ACPI_STATUS status;
    status = AcpiInitializeTables(acpi_tables, ACPI_MAX_INIT_TABLES, FALSE);

    if (status == AE_NOT_FOUND) {
        TRACEF("WARNING: could not find ACPI tables\n");
        return;
    } else if (status == AE_NO_MEMORY) {
        TRACEF("WARNING: could not initialize ACPI tables\n");
        return;
    } else if (status != AE_OK) {
        TRACEF("WARNING: could not initialize ACPI tables for unknown reason\n");
        return;
    }

    acpi_initialized = true;
    LTRACEF("ACPI tables initialized\n");
}

/**
 * @brief  Handle the Power Button Fixed Event
 *
 * We simply write to a well known port. A user-mode driver should pick
 * this event and take action.
 */
static uint32_t power_button_event_handler(void* ctx)
{
    port_packet_t packet = { {"1"} };
    port_write((port_t) ctx, &packet, 1);
    // Note that the spec indicates to return 0. The code in the
    // Intel implementation (AcpiEvFixedEventDetect) reads differently.
    return ACPI_INTERRUPT_HANDLED;
}

/**
 * @brief Initialize the entire ACPI subsystem
 *
 * This subsystem depends on the following subsystems
 * 1) VM
 * 2) Timers
 * 3) Interrupts
 */
void platform_init_acpi(void)
{
    // This sequence is described in section 10.1.2.2 (ACPICA Initialization With
    // Early ACPI Table Access) of the ACPICA developer's reference.
    ACPI_STATUS status = AcpiInitializeSubsystem();
    if (status != AE_OK) {
        TRACEF("WARNING: could not initialize ACPI\n");
        // TODO: Do something better here
        return;
    }

    status = AcpiReallocateRootTable();
    if (status != AE_OK) {
        TRACEF("WARNING: could not reallocate ACPI root table\n");
        // TODO: Do something better here
        return;
    }

    status = AcpiLoadTables();
    if (status != AE_OK) {
        TRACEF("WARNING: could not load ACPI tables\n");
        // TODO: Do something better here
        return;
    }

    // TODO: Install local handlers here

    status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
    if (status != AE_OK) {
        TRACEF("WARNING: could not enable ACPI\n");
        // TODO: Do something better here
        return;
    }

    status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
    if (status != AE_OK) {
        TRACEF("WARNING: could not initialize ACPI objects\n");
        // TODO: Do something better here
        return;
    }

    status = acpi_set_apic_irq_mode();
    if (status == AE_NOT_FOUND) {
        TRACEF("WARNING: Could not find ACPI IRQ mode switch\n");
    } else {
        ASSERT(status == AE_OK);
    }

    port_t power_button_port;
    if (port_create(POWER_BUTTON_PORT, PORT_MODE_BROADCAST,
                    &power_button_port) < 0) {
        TRACEF("Failed to create power button port\n");
        return;
    }

    status = AcpiInstallFixedEventHandler(ACPI_EVENT_POWER_BUTTON,
                                          power_button_event_handler,
                                          power_button_port);
    if (status != AE_OK) {
        printf("Failed to install POWER_BUTTON handler\n");
    }

    LTRACEF("ACPI initialized\n");
}
/* initialize ACPI tables as soon as we have a working VM */
LK_INIT_HOOK(acpi_tables, &platform_init_acpi_tables, LK_INIT_LEVEL_VM + 1);

/* @brief Switch interrupts to APIC model (controls IRQ routing) */
static ACPI_STATUS acpi_set_apic_irq_mode(void)
{
    ACPI_OBJECT selector = {
        .Integer.Type = ACPI_TYPE_INTEGER,
        .Integer.Value = 1, // 1 means APIC mode according to ACPI v5 5.8.1
    };
    ACPI_OBJECT_LIST params = {
        .Count =  1,
        .Pointer = &selector,
    };
    return AcpiEvaluateObject(NULL, (char *)"\\_PIC", &params, NULL);
}

/* Helper routine for translating IRQ routing tables into usable form
 *
 * @param port_dev_id The device ID on the root bus of this root port or
 * UINT8_MAX if this call is for the root bus, not a root port
 * @param port_func_id The function ID on the root bus of this root port or
 * UINT8_MAX if this call is for the root bus, not a root port
 */
static ACPI_STATUS acpi_handle_prt(
        ACPI_HANDLE object,
        struct acpi_pcie_irq_mapping *irq_mapping,
        uint8_t port_dev_id,
        uint8_t port_func_id)
{
    ASSERT((port_dev_id == UINT8_MAX && port_func_id == UINT8_MAX) ||
           (port_dev_id != UINT8_MAX && port_func_id != UINT8_MAX));

    ACPI_BUFFER buffer = {
        // Request that the ACPI subsystem allocate the buffer
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = NULL,
    };
    ACPI_BUFFER crs_buffer = {
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = NULL,
    };

    ACPI_STATUS status = AcpiGetIrqRoutingTable(object, &buffer);
    // IRQ routing tables are *required* to exist on the root hub
    if (status != AE_OK) {
        goto cleanup;
    }
    DEBUG_ASSERT(buffer.Pointer);

    uintptr_t entry_addr = (uintptr_t)buffer.Pointer;
    ACPI_PCI_ROUTING_TABLE *entry;
    for (entry = (ACPI_PCI_ROUTING_TABLE *)entry_addr;
         entry->Length != 0;
         entry_addr += entry->Length, entry = (ACPI_PCI_ROUTING_TABLE *)entry_addr) {

        DEBUG_ASSERT(entry_addr <= (uintptr_t)buffer.Pointer + buffer.Length);
        if (entry->Pin >= PCIE_MAX_LEGACY_IRQ_PINS) {
            return AE_ERROR;
        }
        uint8_t dev_id = (entry->Address >> 16) & (PCIE_MAX_DEVICES_PER_BUS - 1);
        // Either we're handling the root complex (port_dev_id == UINT8_MAX), or
        // we're handling a root port, and if it's a root port, dev_id should
        // be 0.
        ASSERT(port_dev_id == UINT8_MAX || dev_id == 0);

        uint32_t global_irq = ACPI_NO_IRQ_MAPPING;
        bool level_triggered = true;
        bool active_high = false;
        if (entry->Source[0]) {
            // If the Source is not just a NULL byte, then it refers to a
            // PCI Interrupt Link Device
            ACPI_HANDLE ild;
            status = AcpiGetHandle(object, entry->Source, &ild);
            if (status != AE_OK) {
                goto cleanup;
            }
            status = AcpiGetCurrentResources(ild, &crs_buffer);
            if (status != AE_OK) {
                goto cleanup;
            }
            DEBUG_ASSERT(crs_buffer.Pointer);

            uintptr_t crs_entry_addr = (uintptr_t)crs_buffer.Pointer;
            ACPI_RESOURCE *res = (ACPI_RESOURCE *)crs_entry_addr;
            while (res->Type != ACPI_RESOURCE_TYPE_END_TAG) {
                if (res->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
                    ACPI_RESOURCE_EXTENDED_IRQ *irq = &res->Data.ExtendedIrq;
                    if (global_irq != ACPI_NO_IRQ_MAPPING) {
                        // TODO: Handle finding two allocated IRQs.  Shouldn't
                        // happen?
                        PANIC_UNIMPLEMENTED;
                    }
                    if (irq->InterruptCount != 1) {
                        // TODO: Handle finding two allocated IRQs.  Shouldn't
                        // happen?
                        PANIC_UNIMPLEMENTED;
                    }
                    if (irq->Interrupts[0] != 0) {
                        active_high = (irq->Polarity == IRQ_POLARITY_ACTIVE_HIGH);
                        level_triggered = (irq->Triggering == IRQ_TRIGGER_MODE_LEVEL);
                        global_irq = irq->Interrupts[0];
                    }
                } else {
                    // TODO: Handle non extended IRQs
                    PANIC_UNIMPLEMENTED;
                }
                crs_entry_addr += res->Length;
                res = (ACPI_RESOURCE *)crs_entry_addr;
            }
            if (global_irq == ACPI_NO_IRQ_MAPPING) {
                // TODO: Invoke PRS to find what is allocatable and allocate it with SRS
                PANIC_UNIMPLEMENTED;
            }
            AcpiOsFree(crs_buffer.Pointer);
            crs_buffer.Length = ACPI_ALLOCATE_BUFFER;
            crs_buffer.Pointer = NULL;
        } else {
            // Otherwise, SourceIndex refers to a global IRQ number that the pin
            // is connected to
            global_irq = entry->SourceIndex;
        }

        // Check if we've seen this IRQ already, and if so, confirm the
        // IRQ signaling is the same.
        bool found_irq = false;
        for (uint i = 0; i < irq_mapping->num_irqs; ++i) {
            struct acpi_irq_signal *sig = &irq_mapping->irqs[i];
            if (global_irq != sig->global_irq) {
                continue;
            }
            if (active_high != sig->active_high ||
                level_triggered != sig->level_triggered) {

                // TODO: Handle mismatch here
                PANIC_UNIMPLEMENTED;
            }
            found_irq = true;
            break;
        }
        if (!found_irq) {
            ASSERT(irq_mapping->num_irqs < countof(irq_mapping->irqs));
            struct acpi_irq_signal *sig = &irq_mapping->irqs[irq_mapping->num_irqs];
            sig->global_irq = global_irq;
            sig->active_high = active_high;
            sig->level_triggered = level_triggered;
            irq_mapping->num_irqs++;
        }

        if (port_dev_id == UINT8_MAX) {
            for (uint i = 0; i < PCIE_MAX_FUNCTIONS_PER_DEVICE; ++i) {
                irq_mapping->dev_pin_to_global_irq[dev_id][i][entry->Pin] =
                        global_irq;
            }
        } else {
            irq_mapping->dev_pin_to_global_irq[port_dev_id][port_func_id][entry->Pin] = global_irq;
        }
    }

cleanup:
    if (crs_buffer.Pointer) {
        AcpiOsFree(crs_buffer.Pointer);
    }
    if (buffer.Pointer) {
        AcpiOsFree(buffer.Pointer);
    }
    return status;
}

/* @brief Device enumerator for platform_configure_pcie_legacy_irqs */
static ACPI_STATUS acpi_get_pcie_devices_irq(
        ACPI_HANDLE object,
        UINT32 nesting_level,
        void *context,
        void **ret)
{
    struct acpi_pcie_irq_mapping *irq_mapping = context;
    ACPI_STATUS status = acpi_handle_prt(
            object,
            irq_mapping,
            UINT8_MAX,
            UINT8_MAX);
    if (status != AE_OK) {
        return status;
    }

    // Enumerate root ports
    ACPI_HANDLE child = NULL;
    while (1) {
        status = AcpiGetNextObject(ACPI_TYPE_DEVICE, object, child, &child);
        if (status == AE_NOT_FOUND) {
            break;
        } else if (status != AE_OK) {
            printf("Failed to get next child object of root complex\n");
            return status;
        }

        ACPI_OBJECT object = { 0 };
        ACPI_BUFFER buffer = {
            .Length = sizeof(object),
            .Pointer = &object,
        };
        status = AcpiEvaluateObject(child, (char *)"_ADR", NULL, &buffer);
        if (status != AE_OK ||
            buffer.Length < sizeof(object) ||
            object.Type != ACPI_TYPE_INTEGER) {

            continue;
        }
        UINT64 data = object.Integer.Value;
        uint8_t port_dev_id = (data >> 16) & (PCIE_MAX_DEVICES_PER_BUS - 1);
        uint8_t port_func_id = data & (PCIE_MAX_FUNCTIONS_PER_DEVICE - 1);
        // Ignore the return value of this, since if child is not a
        // root port, it will fail and we don't care.
        acpi_handle_prt(
                child,
                irq_mapping,
                port_dev_id,
                port_func_id);
    }
    return AE_OK;
}

/* @brief Find the legacy IRQ swizzling for the PCIe root bus
 *
 * @param root_bus_map The mapping to populate
 *
 * @return NO_ERROR on success
 */
status_t platform_find_pcie_legacy_irq_mapping(struct acpi_pcie_irq_mapping *root_bus_map)
{
    uint map_len = sizeof(root_bus_map->dev_pin_to_global_irq)/sizeof(uint32_t);
    for (uint i = 0; i < map_len; ++i) {
        uint32_t *flat_map = (uint32_t *)&root_bus_map->dev_pin_to_global_irq;
        flat_map[i] = ACPI_NO_IRQ_MAPPING;
    }
    root_bus_map->num_irqs = 0;

    ACPI_STATUS status = AcpiGetDevices(
            (char*)"PNP0A08", // PCIe root hub
            acpi_get_pcie_devices_irq,
            root_bus_map,
            NULL);
    if (status != AE_OK) {
        return ERR_GENERIC;
    }
    return NO_ERROR;
}

void acpi_poweroff(void)
{
    ACPI_STATUS status = AcpiEnterSleepStatePrep(5);
    if (status == AE_OK) {
        AcpiEnterSleepState(5);
    }
}

status_t acpi_get_madt_record_limits(uintptr_t *start, uintptr_t *end)
{
    ACPI_TABLE_HEADER *table = NULL;
    ACPI_STATUS status = AcpiGetTable((char *)ACPI_SIG_MADT, 1, &table);
    if (status != AE_OK) {
        TRACEF("could not find MADT\n");
        return ERR_NOT_FOUND;
    }
    ACPI_TABLE_MADT *madt = (ACPI_TABLE_MADT *)table;
    uintptr_t records_start = ((uintptr_t)madt) + sizeof(*madt);
    uintptr_t records_end = ((uintptr_t)madt) + madt->Header.Length;
    if (records_start >= records_end) {
        TRACEF("MADT wraps around address space\n");
        return ERR_NOT_VALID;
    }
    // Shouldn't be too many records
    if (madt->Header.Length > 4096) {
        TRACEF("MADT suspiciously long: %d\n", madt->Header.Length);
        return ERR_NOT_VALID;
    }
    *start = records_start;
    *end = records_end;
    return NO_ERROR;
}

/* @brief Enumerate all functioning CPUs and their APIC IDs
 *
 * If apic_ids is NULL, just returns the number of logical processors
 * via num_cpus.
 *
 * @param apic_ids Array to write found APIC ids into.
 * @param len Length of apic_ids.
 * @param num_cpus Output for the number of logical processors detected.
 *
 * @return NO_ERROR on success. Note that if len < *num_cpus, not all
 *         logical apic_ids will be returned.
 */
status_t platform_enumerate_cpus(
        uint32_t *apic_ids,
        uint32_t len,
        uint32_t *num_cpus)
{
    if (num_cpus == NULL) {
        return ERR_INVALID_ARGS;
    }

    uintptr_t records_start, records_end;
    status_t status = acpi_get_madt_record_limits(&records_start, &records_end);
    if (status != AE_OK) {
        return status;
    }
    uint32_t count = 0;
    uintptr_t addr;
    ACPI_SUBTABLE_HEADER *record_hdr;
    for (addr = records_start; addr < records_end; addr += record_hdr->Length) {
        record_hdr = (ACPI_SUBTABLE_HEADER *)addr;
        switch (record_hdr->Type) {
            case ACPI_MADT_TYPE_LOCAL_APIC: {
                ACPI_MADT_LOCAL_APIC *lapic = (ACPI_MADT_LOCAL_APIC *)record_hdr;
                if (!(lapic->LapicFlags & ACPI_MADT_ENABLED)) {
                    TRACEF("Skipping disabled processor %02x\n", lapic->Id);
                    continue;
                }
                if (apic_ids != NULL && count < len) {
                    apic_ids[count] = lapic->Id;
                }
                count++;
                break;
            }
        }
    }
    if (addr != records_end) {
      TRACEF("malformed MADT\n");
      return ERR_NOT_VALID;
    }
    *num_cpus = count;
    return NO_ERROR;
}

/* @brief Enumerate all IO APICs
 *
 * If io_apics is NULL, just returns the number of IO APICs
 * via num_io_apics.
 *
 * @param io_apics Array to populate descriptors into.
 * @param len Length of io_apics.
 * @param num_io_apics Number of IO apics found
 *
 * @return NO_ERROR on success. Note that if len < *num_io_apics, not all
 *         IO APICs will be returned.
 */
status_t platform_enumerate_io_apics(
        struct io_apic_descriptor *io_apics,
        uint32_t len,
        uint32_t *num_io_apics)
{
    if (num_io_apics == NULL) {
        return ERR_INVALID_ARGS;
    }

    uintptr_t records_start, records_end;
    status_t status = acpi_get_madt_record_limits(&records_start, &records_end);
    if (status != AE_OK) {
        return status;
    }

    uint32_t count = 0;
    uintptr_t addr;
    for (addr = records_start; addr < records_end;) {
        ACPI_SUBTABLE_HEADER *record_hdr = (ACPI_SUBTABLE_HEADER *)addr;
        switch (record_hdr->Type) {
            case ACPI_MADT_TYPE_IO_APIC: {
                ACPI_MADT_IO_APIC *io_apic = (ACPI_MADT_IO_APIC *)record_hdr;
                if (io_apics != NULL && count < len) {
                    io_apics[count].apic_id = io_apic->Id;
                    io_apics[count].paddr = io_apic->Address;
                    io_apics[count].global_irq_base = io_apic->GlobalIrqBase;
                }
                count++;
                break;
            }
        }

        addr += record_hdr->Length;
    }
    if (addr != records_end) {
      TRACEF("malformed MADT\n");
      return ERR_NOT_VALID;
    }
    *num_io_apics = count;
    return NO_ERROR;
}

/* @brief Enumerate all interrupt source overrides
 *
 * If isos is NULL, just returns the number of ISOs via num_isos.
 *
 * @param isos Array to populate overrides into.
 * @param len Length of isos.
 * @param num_isos Number of ISOs found
 *
 * @return NO_ERROR on success. Note that if len < *num_isos, not all
 *         ISOs will be returned.
 */
status_t platform_enumerate_interrupt_source_overrides(
        struct io_apic_isa_override *isos,
        uint32_t len,
        uint32_t *num_isos)
{
    if (num_isos == NULL) {
        return ERR_INVALID_ARGS;
    }

    uintptr_t records_start, records_end;
    status_t status = acpi_get_madt_record_limits(&records_start, &records_end);
    if (status != AE_OK) {
        return status;
    }

    uint32_t count = 0;
    uintptr_t addr;
    for (addr = records_start; addr < records_end;) {
        ACPI_SUBTABLE_HEADER *record_hdr = (ACPI_SUBTABLE_HEADER *)addr;
        switch (record_hdr->Type) {
            case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE: {
                ACPI_MADT_INTERRUPT_OVERRIDE *iso =
                        (ACPI_MADT_INTERRUPT_OVERRIDE *)record_hdr;
                if (isos != NULL && count < len) {
                    ASSERT(iso->Bus == 0); // 0 means ISA, ISOs are only ever for ISA IRQs
                    isos[count].isa_irq = iso->SourceIrq;
                    isos[count].remapped = true;
                    isos[count].global_irq = iso->GlobalIrq;

                    uint32_t flags = iso->IntiFlags;
                    uint32_t polarity = flags & ACPI_MADT_POLARITY_MASK;
                    uint32_t trigger = flags & ACPI_MADT_TRIGGER_MASK;

                    // Conforms below means conforms to the bus spec.  ISA is
                    // edge triggered and active high.
                    switch (polarity) {
                        case ACPI_MADT_POLARITY_CONFORMS:
                        case ACPI_MADT_POLARITY_ACTIVE_HIGH:
                            isos[count].pol = IRQ_POLARITY_ACTIVE_HIGH;
                            break;
                        case ACPI_MADT_POLARITY_ACTIVE_LOW:
                            isos[count].pol = IRQ_POLARITY_ACTIVE_LOW;
                            break;
                        default:
                            panic("Unknown IRQ polarity in override: %d\n",
                                  polarity);
                    }

                    switch (trigger) {
                        case ACPI_MADT_TRIGGER_CONFORMS:
                        case ACPI_MADT_TRIGGER_EDGE:
                            isos[count].tm = IRQ_TRIGGER_MODE_EDGE;
                            break;
                        case ACPI_MADT_TRIGGER_LEVEL:
                            isos[count].tm = IRQ_TRIGGER_MODE_LEVEL;
                            break;
                        default:
                            panic("Unknown IRQ trigger in override: %d\n",
                                  trigger);
                    }
                }
                count++;
                break;
            }
        }

        addr += record_hdr->Length;
    }
    if (addr != records_end) {
      TRACEF("malformed MADT\n");
      return ERR_NOT_VALID;
    }
    *num_isos = count;
    return NO_ERROR;
}

/* @brief Find the PCIE config (returns the first one found)
 *
 * @param config The structure to populate with the found config.
 *
 * @return NO_ERROR on success.
 */
status_t platform_find_pcie_config(struct acpi_pcie_config *config)
{
    ACPI_TABLE_HEADER *raw_table = NULL;
    ACPI_STATUS status = AcpiGetTable((char *)ACPI_SIG_MCFG, 1, &raw_table);
    if (status != AE_OK) {
        TRACEF("could not find MCFG\n");
        return ERR_NOT_FOUND;
    }
    ACPI_TABLE_MCFG *mcfg = (ACPI_TABLE_MCFG *)raw_table;
    ACPI_MCFG_ALLOCATION *table_start = ((void *)mcfg) + sizeof(*mcfg);
    ACPI_MCFG_ALLOCATION *table_end = ((void *)mcfg) + mcfg->Header.Length;
    uintptr_t table_bytes = (uintptr_t)table_end - (uintptr_t)table_start;
    if (table_bytes % sizeof(*table_start) != 0) {
        TRACEF("MCFG has unexpected size\n");
        return ERR_NOT_VALID;
    }
    int num_entries = table_end - table_start;
    if (num_entries == 0) {
        TRACEF("MCFG has no entries\n");
        return ERR_NOT_FOUND;
    }
    if (num_entries > 1) {
        TRACEF("MCFG has more than one entry, just taking the first\n");
    }

    size_t size_per_bus = PCIE_EXTENDED_CONFIG_SIZE *
            PCIE_MAX_DEVICES_PER_BUS * PCIE_MAX_FUNCTIONS_PER_DEVICE;
    int num_buses = table_start->EndBusNumber - table_start->StartBusNumber + 1;

    config->segment_group = table_start->PciSegment;
    config->start_bus = table_start->StartBusNumber;
    config->end_bus = table_start->EndBusNumber;
    // We need to adjust the physical address we received to align to the proper
    // bus number.
    //
    // Citation from PCI Firmware Spec 3.0:
    // For PCI-X and PCI Express platforms utilizing the enhanced
    // configuration access method, the base address of the memory mapped
    // configuration space always corresponds to bus number 0 (regardless
    // of the start bus number decoded by the host bridge).
    config->ecam_phys = table_start->Address + size_per_bus * config->start_bus;
    // The size of this mapping is defined in the PCI Firmware v3 spec to be
    // big enough for all of the buses in this config.
    config->ecam_size = size_per_bus * num_buses;
    return NO_ERROR;
}
