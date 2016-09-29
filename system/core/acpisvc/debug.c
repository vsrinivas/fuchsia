// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <acpica/acpi.h>

static inline void do_indent(unsigned int level) {
    while (level) {
        printf("  ");
        level--;
    }
}

#define INDENT_PRINTF(...)   \
    do {                     \
        do_indent(level);    \
        printf(__VA_ARGS__); \
    } while (0)

enum print_resource_request {
    CURRENT_RESOURCES,
    POSSIBLE_RESOURCES,
};

static ACPI_STATUS acpi_print_resources(
    ACPI_HANDLE object,
    unsigned int level,
    enum print_resource_request type) {
    ACPI_BUFFER buffer = {
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = NULL,
    };
    ACPI_STATUS status = AE_BAD_PARAMETER;
    if (type == POSSIBLE_RESOURCES) {
        status = AcpiGetPossibleResources(object, &buffer);
    } else if (type == CURRENT_RESOURCES) {
        status = AcpiGetCurrentResources(object, &buffer);
    } else {
        printf("Invalid resource type to print\n");
        return AE_BAD_PARAMETER;
        ;
    }

    if (status != AE_OK) {
        if (buffer.Pointer) {
            AcpiOsFree(buffer.Pointer);
        }
        return status;
    }
    if (type == POSSIBLE_RESOURCES) {
        INDENT_PRINTF("PRS:\n");
    } else if (type == CURRENT_RESOURCES) {
        INDENT_PRINTF("CRS:\n");
    }

    uintptr_t entry_addr = (uintptr_t)buffer.Pointer;
    ACPI_RESOURCE* res = (ACPI_RESOURCE*)entry_addr;
    level += 1;
    while (res->Type != ACPI_RESOURCE_TYPE_END_TAG) {
        INDENT_PRINTF("Entry: ");
        level += 1;
        switch (res->Type) {
        case ACPI_RESOURCE_TYPE_IO: {
            printf("IO\n");
            ACPI_RESOURCE_IO* io = &res->Data.Io;
            INDENT_PRINTF("io_decode: %d\n", io->IoDecode);
            INDENT_PRINTF("alignment: %d\n", io->Alignment);
            INDENT_PRINTF("addrlen: %d\n", io->AddressLength);
            INDENT_PRINTF("address min: %#04x\n", io->Minimum);
            INDENT_PRINTF("address max: %#04x\n", io->Maximum);
            break;
        }
        case ACPI_RESOURCE_TYPE_ADDRESS16: {
            printf("Address16\n");
            ACPI_RESOURCE_ADDRESS16* a16 = &res->Data.Address16;
            INDENT_PRINTF("res_type: %d\n", a16->ResourceType);
            INDENT_PRINTF("produce_consume: %d\n", a16->ProducerConsumer);
            INDENT_PRINTF("decode: %d\n", a16->Decode);
            INDENT_PRINTF("min_addr_fixed: %d\n", a16->MinAddressFixed);
            INDENT_PRINTF("max_addr_fixed: %d\n", a16->MaxAddressFixed);
            INDENT_PRINTF("address granularity: %#04x\n", a16->Address.Granularity);
            INDENT_PRINTF("address min: %#04x\n", a16->Address.Minimum);
            INDENT_PRINTF("address max: %#04x\n", a16->Address.Maximum);
            INDENT_PRINTF("address xlat offset: %#04x\n", a16->Address.TranslationOffset);
            INDENT_PRINTF("address len: %#04x\n", a16->Address.AddressLength);
            // TODO: extract MTRR info from a16->Info
            break;
        }
        case ACPI_RESOURCE_TYPE_ADDRESS32: {
            printf("Address32\n");
            ACPI_RESOURCE_ADDRESS32* a32 = &res->Data.Address32;
            INDENT_PRINTF("res_type: %d\n", a32->ResourceType);
            INDENT_PRINTF("produce_consume: %d\n", a32->ProducerConsumer);
            INDENT_PRINTF("decode: %d\n", a32->Decode);
            INDENT_PRINTF("min_addr_fixed: %d\n", a32->MinAddressFixed);
            INDENT_PRINTF("max_addr_fixed: %d\n", a32->MaxAddressFixed);
            INDENT_PRINTF("address granularity: %#08x\n", a32->Address.Granularity);
            INDENT_PRINTF("address min: %#08x\n", a32->Address.Minimum);
            INDENT_PRINTF("address max: %#08x\n", a32->Address.Maximum);
            INDENT_PRINTF("address xlat offset: %#08x\n", a32->Address.TranslationOffset);
            INDENT_PRINTF("address len: %#08x\n", a32->Address.AddressLength);
            // TODO: extract MTRR info from a32->Info
            break;
        }
        case ACPI_RESOURCE_TYPE_IRQ: {
            printf("IRQ\n");
            ACPI_RESOURCE_IRQ* irq = &res->Data.Irq;
            INDENT_PRINTF("trigger: %s\n", irq->Triggering == ACPI_EDGE_SENSITIVE ? "edge" : "level");
            const char* pol = "invalid";
            switch (irq->Polarity) {
            case ACPI_ACTIVE_BOTH:
                pol = "both";
                break;
            case ACPI_ACTIVE_LOW:
                pol = "low";
                break;
            case ACPI_ACTIVE_HIGH:
                pol = "high";
                break;
            }
            INDENT_PRINTF("polarity: %s\n", pol);
            INDENT_PRINTF("sharable: %d\n", irq->Sharable);
            INDENT_PRINTF("wake_cap: %d\n", irq->WakeCapable);
            for (unsigned int i = 0; i < irq->InterruptCount; ++i) {
                INDENT_PRINTF("irq #%d: %d\n", i, irq->Interrupts[i]);
            }
            break;
        }
        case ACPI_RESOURCE_TYPE_EXTENDED_IRQ: {
            printf("Extended IRQ\n");
            ACPI_RESOURCE_EXTENDED_IRQ* irq = &res->Data.ExtendedIrq;
            INDENT_PRINTF("produce_consume: %d\n", irq->ProducerConsumer);
            INDENT_PRINTF("trigger: %s\n", irq->Triggering == ACPI_EDGE_SENSITIVE ? "edge" : "level");
            const char* pol = "invalid";
            switch (irq->Polarity) {
            case ACPI_ACTIVE_BOTH:
                pol = "both";
                break;
            case ACPI_ACTIVE_LOW:
                pol = "low";
                break;
            case ACPI_ACTIVE_HIGH:
                pol = "high";
                break;
            }
            INDENT_PRINTF("polarity: %s\n", pol);
            INDENT_PRINTF("sharable: %d\n", irq->Sharable);
            INDENT_PRINTF("wake_cap: %d\n", irq->WakeCapable);
            for (unsigned int i = 0; i < irq->InterruptCount; ++i) {
                INDENT_PRINTF("irq #%d: %d\n", i, irq->Interrupts[i]);
            }
            break;
        }
        default:
            printf("Unknown (type %u)\n", res->Type);
        }
        level -= 1;

        entry_addr += res->Length;
        res = (ACPI_RESOURCE*)entry_addr;
    }
    level -= 1;

    AcpiOsFree(buffer.Pointer);
    return AE_OK;
}

static ACPI_STATUS acpi_get_pcie_devices_crs(
    ACPI_HANDLE object,
    UINT32 nesting_level,
    void* context,
    void** ret) {
    printf("Found object %p\n", object);
    return acpi_print_resources(object, 1, CURRENT_RESOURCES);
}

static void acpi_debug_pcie_crs(void) {
    ACPI_STATUS status = AcpiGetDevices(
        (char*)"PNP0A08",
        acpi_get_pcie_devices_crs,
        NULL,
        NULL);
    if (status != AE_OK) {
        printf("Could not find PCIe root complex\n");
    }
}

static ACPI_STATUS acpi_print_prt(unsigned int level, ACPI_HANDLE object) {
    ACPI_STATUS status = AE_OK;

    ACPI_BUFFER buffer = {
        // Request that the ACPI subsystem allocate the buffer
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = NULL,
    };
    status = AcpiGetIrqRoutingTable(object, &buffer);
    if (status != AE_OK) {
        if (buffer.Pointer) {
            AcpiOsFree(buffer.Pointer);
        }
        return status;
    }
    assert(buffer.Pointer);

    uintptr_t entry_addr = (uintptr_t)buffer.Pointer;
    ACPI_PCI_ROUTING_TABLE* entry;
    for (entry = (ACPI_PCI_ROUTING_TABLE*)entry_addr;
         entry->Length != 0;
         entry_addr += entry->Length, entry = (ACPI_PCI_ROUTING_TABLE*)entry_addr) {

        assert(entry_addr <= (uintptr_t)buffer.Pointer + buffer.Length);

        INDENT_PRINTF("Entry:\n");
        level += 1;
        if (entry->Pin > 3) {
            INDENT_PRINTF("Pin: Invalid (%08x)\n", entry->Pin);
        } else {
            INDENT_PRINTF("Pin: INT%c\n", 'A' + entry->Pin);
        }
        INDENT_PRINTF("Address: %#016llx\n", entry->Address);
        level += 1;
        INDENT_PRINTF("Dev ID: %#04x\n", (uint16_t)(entry->Address >> 16));
        level -= 1;

        if (entry->Source[0]) {
            // If the Source is not just a NULL byte, then it refers to a
            // PCI Interrupt Link Device
            INDENT_PRINTF("Source: %s\n", entry->Source);
            INDENT_PRINTF("Source Index: %u\n", entry->SourceIndex);
            ACPI_HANDLE ild;
            status = AcpiGetHandle(object, entry->Source, &ild);
            if (status != AE_OK) {
                INDENT_PRINTF("Could not lookup Interrupt Link Device\n");
                continue;
            }
            status = acpi_print_resources(ild, 2, CURRENT_RESOURCES);
            if (status != AE_OK) {
                INDENT_PRINTF("Could not lookup ILD CRS\n");
            }
            status = acpi_print_resources(ild, 2, POSSIBLE_RESOURCES);
            if (status != AE_OK) {
                INDENT_PRINTF("Could not lookup ILD PRS\n");
            }
        } else {
            // Otherwise, it just refers to a global IRQ number that the pin
            // is connected to
            INDENT_PRINTF("GlobalIRQ: %u\n", entry->SourceIndex);
        }
        level -= 1;
    }

    AcpiOsFree(buffer.Pointer);
    return AE_OK;
}

static ACPI_STATUS acpi_get_pcie_devices_irq(
    ACPI_HANDLE object,
    UINT32 nesting_level,
    void* context,
    void** ret) {
    ACPI_STATUS status = acpi_print_prt(nesting_level, object);
    if (status != AE_OK) {
        printf("Failed to print PRT for root complex\n");
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
        unsigned int level = nesting_level;
        INDENT_PRINTF(
            "Device %#02x Function %#01x:\n",
            (uint8_t)(data >> 16),
            (uint8_t)(data & 0x7));
        status = acpi_print_prt(nesting_level + 1, child);
        if (status != AE_OK) {
            continue;
        }
    }

    return AE_OK;
}

static void acpi_debug_pcie_irq_routing(void) {
    ACPI_STATUS status = AcpiGetDevices(
        (char*)"PNP0A08",
        acpi_get_pcie_devices_irq,
        NULL,
        NULL);
    if (status != AE_OK) {
        printf("Could not enumerate PRTs\n");
    }
}

static ACPI_STATUS acpi_debug_print_device_name(
    ACPI_HANDLE object,
    UINT32 nesting_level,
    void* context,
    void** ret) {
    ACPI_DEVICE_INFO* info = NULL;
    ACPI_STATUS status = AcpiGetObjectInfo(object, &info);
    if (status != AE_OK) {
        if (info) {
            ACPI_FREE(info);
        }
        return status;
    }

    unsigned int level = nesting_level;
    INDENT_PRINTF("%4s\n", (char*)&info->Name);

    ACPI_FREE(info);
    return AE_OK;
}

static void acpi_debug_walk_ns(void) {
    ACPI_STATUS status = AcpiWalkNamespace(
        ACPI_TYPE_DEVICE,
        ACPI_ROOT_OBJECT,
        INT_MAX,
        acpi_debug_print_device_name,
        NULL,
        NULL,
        NULL);
    if (status != AE_OK) {
        printf("Failed to walk namespace\n");
    }
}
