// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/compiler.h>

#include <acpica/acpi.h>

#include <assert.h>
#include <err.h>
#include <trace.h>

#include <lk/init.h>

#include <arch/x86/apic.h>
#include <platform/pc/acpi.h>

#define LOCAL_TRACE 0

#define ACPI_MAX_INIT_TABLES 32
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

/* initialize ACPI tables as soon as we have a working VM */
LK_INIT_HOOK(acpi_tables, &platform_init_acpi_tables, LK_INIT_LEVEL_VM + 1);

static status_t acpi_get_madt_record_limits(uintptr_t *start, uintptr_t *end)
{
    ACPI_TABLE_HEADER *table = NULL;
    ACPI_STATUS status = AcpiGetTable((char *)ACPI_SIG_MADT, 1, &table);
    if (status != AE_OK) {
        TRACEF("could not find MADT\n");
        return MX_ERR_NOT_FOUND;
    }
    ACPI_TABLE_MADT *madt = (ACPI_TABLE_MADT *)table;
    uintptr_t records_start = ((uintptr_t)madt) + sizeof(*madt);
    uintptr_t records_end = ((uintptr_t)madt) + madt->Header.Length;
    if (records_start >= records_end) {
        TRACEF("MADT wraps around address space\n");
        return MX_ERR_INTERNAL;
    }
    // Shouldn't be too many records
    if (madt->Header.Length > 4096) {
        TRACEF("MADT suspiciously long: %u\n", madt->Header.Length);
        return MX_ERR_INTERNAL;
    }
    *start = records_start;
    *end = records_end;
    return MX_OK;
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
 * @return MX_OK on success. Note that if len < *num_cpus, not all
 *         logical apic_ids will be returned.
 */
status_t platform_enumerate_cpus(
        uint32_t *apic_ids,
        uint32_t len,
        uint32_t *num_cpus)
{
    if (num_cpus == NULL) {
        return MX_ERR_INVALID_ARGS;
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
      return MX_ERR_INTERNAL;
    }
    *num_cpus = count;
    return MX_OK;
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
 * @return MX_OK on success. Note that if len < *num_io_apics, not all
 *         IO APICs will be returned.
 */
status_t platform_enumerate_io_apics(
        struct io_apic_descriptor *io_apics,
        uint32_t len,
        uint32_t *num_io_apics)
{
    if (num_io_apics == NULL) {
        return MX_ERR_INVALID_ARGS;
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
      return MX_ERR_INVALID_ARGS;
    }
    *num_io_apics = count;
    return MX_OK;
}

/* @brief Enumerate all interrupt source overrides
 *
 * If isos is NULL, just returns the number of ISOs via num_isos.
 *
 * @param isos Array to populate overrides into.
 * @param len Length of isos.
 * @param num_isos Number of ISOs found
 *
 * @return MX_OK on success. Note that if len < *num_isos, not all
 *         ISOs will be returned.
 */
status_t platform_enumerate_interrupt_source_overrides(
        struct io_apic_isa_override *isos,
        uint32_t len,
        uint32_t *num_isos)
{
    if (num_isos == NULL) {
        return MX_ERR_INVALID_ARGS;
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
                            panic("Unknown IRQ polarity in override: %u\n",
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
                            panic("Unknown IRQ trigger in override: %u\n",
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
      return MX_ERR_INVALID_ARGS;
    }
    *num_isos = count;
    return MX_OK;
}

/* @brief Return information about the High Precision Event Timer, if present.
 *
 * @param hpet Descriptor to populate
 *
 * @return MX_OK on success.
 */
status_t platform_find_hpet(struct acpi_hpet_descriptor *hpet)
{
    ACPI_TABLE_HEADER *table = NULL;
    ACPI_STATUS status = AcpiGetTable((char *)ACPI_SIG_HPET, 1, &table);
    if (status != AE_OK) {
        TRACEF("could not find HPET\n");
        return MX_ERR_NOT_FOUND;
    }
    ACPI_TABLE_HPET *hpet_tbl = (ACPI_TABLE_HPET *)table;
    if (hpet_tbl->Header.Length != sizeof(ACPI_TABLE_HPET)) {
        TRACEF("Unexpected HPET table length\n");
        return MX_ERR_NOT_FOUND;
    }

    hpet->minimum_tick = hpet_tbl->MinimumTick;
    hpet->sequence = hpet_tbl->Sequence;
    hpet->address = hpet_tbl->Address.Address;
    switch (hpet_tbl->Address.SpaceId) {
        case ACPI_ADR_SPACE_SYSTEM_IO: hpet->port_io = true; break;
        case ACPI_ADR_SPACE_SYSTEM_MEMORY: hpet->port_io = false; break;
        default: return MX_ERR_NOT_SUPPORTED;
    }

    return MX_OK;
}
