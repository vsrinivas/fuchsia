// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pci.h"

#include <assert.h>
#include <mxio/debug.h>
#include <stdio.h>

#include <acpica/acpi.h>

#include "resources.h"

#define MXDEBUG 0

#define PCIE_MAX_LEGACY_IRQ_PINS 4
#define PCIE_MAX_DEVICES_PER_BUS 32
#define PCIE_MAX_FUNCTIONS_PER_DEVICE 8
#define PCIE_EXTENDED_CONFIG_SIZE 4096

#define PANIC_UNIMPLEMENTED __builtin_trap()

/* Helper routine for translating IRQ routing tables into usable form
 *
 * @param port_dev_id The device ID on the root bus of this root port or
 * UINT8_MAX if this call is for the root bus, not a root port
 * @param port_func_id The function ID on the root bus of this root port or
 * UINT8_MAX if this call is for the root bus, not a root port
 */
static ACPI_STATUS handle_prt(
    ACPI_HANDLE object,
    mx_pci_init_arg_t* arg,
    uint8_t port_dev_id,
    uint8_t port_func_id) {
    assert((port_dev_id == UINT8_MAX && port_func_id == UINT8_MAX) ||
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

    uintptr_t entry_addr = (uintptr_t)buffer.Pointer;
    ACPI_PCI_ROUTING_TABLE* entry;
    for (entry = (ACPI_PCI_ROUTING_TABLE*)entry_addr;
         entry->Length != 0;
         entry_addr += entry->Length, entry = (ACPI_PCI_ROUTING_TABLE*)entry_addr) {

        if (entry_addr > (uintptr_t)buffer.Pointer + buffer.Length) {
            return AE_ERROR;
        }
        if (entry->Pin >= PCIE_MAX_LEGACY_IRQ_PINS) {
            return AE_ERROR;
        }
        uint8_t dev_id = (entry->Address >> 16) & (PCIE_MAX_DEVICES_PER_BUS - 1);
        // Either we're handling the root complex (port_dev_id == UINT8_MAX), or
        // we're handling a root port, and if it's a root port, dev_id should
        // be 0.
        if (port_dev_id != UINT8_MAX && dev_id != 0) {
            // this is a weird entry, skip it
            continue;
        }

        uint32_t global_irq = MX_PCI_NO_IRQ_MAPPING;
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

            uintptr_t crs_entry_addr = (uintptr_t)crs_buffer.Pointer;
            ACPI_RESOURCE* res = (ACPI_RESOURCE*)crs_entry_addr;
            while (res->Type != ACPI_RESOURCE_TYPE_END_TAG) {
                if (res->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
                    ACPI_RESOURCE_EXTENDED_IRQ* irq = &res->Data.ExtendedIrq;
                    if (global_irq != MX_PCI_NO_IRQ_MAPPING) {
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
                        active_high = (irq->Polarity == ACPI_ACTIVE_HIGH);
                        level_triggered = (irq->Triggering == ACPI_LEVEL_SENSITIVE);
                        global_irq = irq->Interrupts[0];
                    }
                } else {
                    // TODO: Handle non extended IRQs
                    PANIC_UNIMPLEMENTED;
                }
                crs_entry_addr += res->Length;
                res = (ACPI_RESOURCE*)crs_entry_addr;
            }
            if (global_irq == MX_PCI_NO_IRQ_MAPPING) {
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
        for (unsigned int i = 0; i < arg->num_irqs; ++i) {
            if (global_irq != arg->irqs[i].global_irq) {
                continue;
            }
            if (active_high != arg->irqs[i].active_high ||
                level_triggered != arg->irqs[i].level_triggered) {

                // TODO: Handle mismatch here
                PANIC_UNIMPLEMENTED;
            }
            found_irq = true;
            break;
        }
        if (!found_irq) {
            assert(arg->num_irqs < countof(arg->irqs));
            arg->irqs[arg->num_irqs].global_irq = global_irq;
            arg->irqs[arg->num_irqs].active_high = active_high;
            arg->irqs[arg->num_irqs].level_triggered = level_triggered;
            arg->num_irqs++;
        }

        if (port_dev_id == UINT8_MAX) {
            for (unsigned int i = 0; i < PCIE_MAX_FUNCTIONS_PER_DEVICE; ++i) {
                arg->dev_pin_to_global_irq[dev_id][i][entry->Pin] =
                    global_irq;
            }
        } else {
            arg->dev_pin_to_global_irq[port_dev_id][port_func_id][entry->Pin] = global_irq;
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
static ACPI_STATUS get_pcie_devices_irq(
    ACPI_HANDLE object,
    UINT32 nesting_level,
    void* context,
    void** ret) {
    mx_pci_init_arg_t* arg = context;
    ACPI_STATUS status = handle_prt(
        object,
        arg,
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
            return status;
        }

        ACPI_OBJECT object = {0};
        ACPI_BUFFER buffer = {
            .Length = sizeof(object),
            .Pointer = &object,
        };
        status = AcpiEvaluateObject(child, (char*)"_ADR", NULL, &buffer);
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
        handle_prt(
            child,
            arg,
            port_dev_id,
            port_func_id);
    }
    return AE_OK;
}

/* @brief Find the legacy IRQ swizzling for the PCIe root bus
 *
 * @param arg The structure to populate
 *
 * @return NO_ERROR on success
 */
static mx_status_t find_pcie_legacy_irq_mapping(mx_pci_init_arg_t* arg) {
    unsigned int map_len = sizeof(arg->dev_pin_to_global_irq) / sizeof(uint32_t);
    for (unsigned int i = 0; i < map_len; ++i) {
        uint32_t* flat_map = (uint32_t*)&arg->dev_pin_to_global_irq;
        flat_map[i] = MX_PCI_NO_IRQ_MAPPING;
    }
    arg->num_irqs = 0;

    ACPI_STATUS status = AcpiGetDevices(
        (char*)"PNP0A08", // PCIe root hub
        get_pcie_devices_irq,
        arg,
        NULL);
    if (status != AE_OK) {
        return ERR_INTERNAL;
    }
    return NO_ERROR;
}

/* @brief Find the PCIe config (returns the first one found)
 *
 * @param arg The structure to populate
 *
 * @return NO_ERROR on success.
 */
static mx_status_t find_pcie_config(mx_pci_init_arg_t* arg) {
    ACPI_TABLE_HEADER* raw_table = NULL;
    ACPI_STATUS status = AcpiGetTable((char*)ACPI_SIG_MCFG, 1, &raw_table);
    if (status != AE_OK) {
        xprintf("could not find MCFG\n");
        return ERR_NOT_FOUND;
    }
    ACPI_TABLE_MCFG* mcfg = (ACPI_TABLE_MCFG*)raw_table;
    ACPI_MCFG_ALLOCATION* table_start = ((void*)mcfg) + sizeof(*mcfg);
    ACPI_MCFG_ALLOCATION* table_end = ((void*)mcfg) + mcfg->Header.Length;
    uintptr_t table_bytes = (uintptr_t)table_end - (uintptr_t)table_start;
    if (table_bytes % sizeof(*table_start) != 0) {
        xprintf("MCFG has unexpected size\n");
        return ERR_INTERNAL;
    }
    int num_entries = table_end - table_start;
    if (num_entries == 0) {
        xprintf("MCFG has no entries\n");
        return ERR_NOT_FOUND;
    }
    if (num_entries > 1) {
        xprintf("MCFG has more than one entry, just taking the first\n");
    }

    size_t size_per_bus = PCIE_EXTENDED_CONFIG_SIZE *
                          PCIE_MAX_DEVICES_PER_BUS * PCIE_MAX_FUNCTIONS_PER_DEVICE;
    int num_buses = table_start->EndBusNumber - table_start->StartBusNumber + 1;

    if (table_start->PciSegment != 0) {
        xprintf("Non-zero segment found\n");
        return ERR_NOT_SUPPORTED;
    }

    arg->ecam_windows[0].bus_start = table_start->StartBusNumber;
    arg->ecam_windows[0].bus_end = table_start->EndBusNumber;

    // We need to adjust the physical address we received to align to the proper
    // bus number.
    //
    // Citation from PCI Firmware Spec 3.0:
    // For PCI-X and PCI Express platforms utilizing the enhanced
    // configuration access method, the base address of the memory mapped
    // configuration space always corresponds to bus number 0 (regardless
    // of the start bus number decoded by the host bridge).
    arg->ecam_windows[0].base = table_start->Address + size_per_bus * arg->ecam_windows[0].bus_start;
    // The size of this mapping is defined in the PCI Firmware v3 spec to be
    // big enough for all of the buses in this config.
    arg->ecam_windows[0].size = size_per_bus * num_buses;
    arg->ecam_window_count = 1;
    return NO_ERROR;
}

/* @brief Compute PCIe initialization information
 *
 * The computed initialization information can be released with free()
 *
 * @param arg Pointer to store the initialization information into
 *
 * @return NO_ERROR on success
 */
mx_status_t get_pci_init_arg(mx_pci_init_arg_t** arg, uint32_t* size) {
    mx_pci_init_arg_t* res = NULL;

    // TODO(teisenbe): We assume only one ECAM window right now...
    size_t obj_size = sizeof(*res) + sizeof(res->ecam_windows[0]) * 1;
    res = calloc(1, obj_size);
    if (!res) {
        return ERR_NO_MEMORY;
    }

    mx_status_t status = find_pcie_config(res);
    if (status != NO_ERROR) {
        goto fail;
    }

    status = find_pcie_legacy_irq_mapping(res);
    if (status != NO_ERROR) {
        goto fail;
    }

    *arg = res;
    *size = sizeof(*res) + sizeof(res->ecam_windows[0]) * res->ecam_window_count;
    return NO_ERROR;
fail:
    free(res);
    return status;
}

struct report_current_resources_ctx {
    mx_handle_t pci_handle;
    bool device_is_root_bridge;
};

static ACPI_STATUS report_current_resources_resource_cb(ACPI_RESOURCE* res, void* _ctx) {
    struct report_current_resources_ctx* ctx = _ctx;

    bool is_mmio = false;
    uint64_t base = 0;
    uint64_t len = 0;
    bool add_range = false;

    if (resource_is_memory(res)) {
        resource_memory_t mem;
        mx_status_t status = resource_parse_memory(res, &mem);
        if (status != NO_ERROR || mem.minimum != mem.maximum) {
            return AE_ERROR;
        }

        is_mmio = true;
        base = mem.minimum;
        len = mem.address_length;
    } else if (resource_is_address(res)) {
        resource_address_t addr;
        mx_status_t status = resource_parse_address(res, &addr);
        if (status != NO_ERROR) {
            return AE_ERROR;
        }

        if (addr.resource_type == RESOURCE_ADDRESS_MEMORY) {
            is_mmio = true;
        } else if (addr.resource_type == RESOURCE_ADDRESS_IO) {
            is_mmio = false;
        } else {
            return AE_OK;
        }

        if (!addr.min_address_fixed || !addr.max_address_fixed || addr.maximum < addr.minimum) {
            printf("WARNING: ACPI found bad _CRS entry\n");
            return AE_OK;
        }

        // We compute len from maximum rather than address_length, since some
        // implementations don't set address_length...
        base = addr.minimum;
        len = addr.maximum - base + 1;

        if (ctx->device_is_root_bridge && !addr.consumed_only) {
            add_range = true;
        }
    } else if (resource_is_io(res)) {
        resource_io_t io;
        mx_status_t status = resource_parse_io(res, &io);
        if (status != NO_ERROR || io.minimum != io.maximum) {
            return AE_ERROR;
        }

        is_mmio = false;
        base = io.minimum;
        len = io.address_length;
    } else {
        return AE_OK;
    }

    // Ignore empty regions that are reported, and don't try adding regions.
    // Skipping adds is a conservative choice to protect against regions that
    // are under PCIe, but are fixed reservations for other things (e.g. the low
    // memory graphics window that is decoded by ISA).
    //
    // If we decide to process adds in the future, it should be done in two
    // passes.  First adding in adds we find, then subtracting out things that
    // are being consumed elsewhere.  This will force an ordering on the
    // operations, and be a conservative protection against inconsistent
    // information in the _CRS tables.
    if (len == 0 || add_range == true) {
        return AE_OK;
    }

    xprintf("ACPI range modification: %sing %s %016lx %016lx\n",
            add_range ? "add" : "subtract", is_mmio ? "MMIO" : "PIO", base, len);

    mx_status_t status = mx_pci_add_subtract_io_range(
            ctx->pci_handle, is_mmio, base, len, add_range);
    if (status != NO_ERROR) {
        // If we are subtracting a range and fail, abort.  This is bad.
        return AE_ERROR;
    }
    return AE_OK;
}

static ACPI_STATUS report_current_resources_device_cb(
        ACPI_HANDLE object, uint32_t nesting_level, void* _ctx, void** ret) {

    ACPI_DEVICE_INFO* info = NULL;
    ACPI_STATUS status = AcpiGetObjectInfo(object, &info);
    if (status != AE_OK) {
        return status;
    }

    struct report_current_resources_ctx* ctx = _ctx;
    ctx->device_is_root_bridge = (info->Flags & ACPI_PCI_ROOT_BRIDGE) != 0;

    ACPI_FREE(info);


    status = AcpiWalkResources(object, (char*)"_CRS", report_current_resources_resource_cb, ctx);
    if (status == AE_NOT_FOUND || status == AE_OK) {
        return AE_OK;
    }
    return status;
}

/* @brief Report current resources to the kernel PCI driver
 *
 * Walks the ACPI namespace and use the reported current resources to inform
   the kernel PCI interface about what memory it shouldn't use.
 *
 * @param root_resource_handle The handle to pass to the kernel when talking
 * to the PCI driver.
 *
 * @return NO_ERROR on success
 */
mx_status_t pci_report_current_resources(mx_handle_t root_resource_handle) {
    struct report_current_resources_ctx ctx = {
        .pci_handle = root_resource_handle,
        .device_is_root_bridge = false,
    };

    // Walk the device tree and integrate found resources into the PCIe IO
    // ranges (in particular, removing ones found to be in use).
    ACPI_STATUS status = AcpiGetDevices(NULL, report_current_resources_device_cb, &ctx, NULL);
    if (status != AE_OK) {
        return ERR_INTERNAL;
    }

    return NO_ERROR;
}
