// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <acpica/acpi.h>
#include <acpica/accommon.h>
#include <acpica/acnamesp.h>
#include <assert.h>
#include <err.h>

#include <arch/x86/apic.h>
#include <lib/console.h>
#include <lk/init.h>
#include <kernel/port.h>
#include <platform/acpi.h>

extern status_t acpi_get_madt_record_limits(uintptr_t *start, uintptr_t *end);

static void acpi_debug_madt(void)
{
    uintptr_t records_start, records_end;
    status_t status = acpi_get_madt_record_limits(&records_start, &records_end);
    if (status != AE_OK) {
        printf("Invalid MADT\n");
        return;
    }

    uintptr_t addr;
    for (addr = records_start; addr < records_end;) {
        ACPI_SUBTABLE_HEADER *record_hdr = (ACPI_SUBTABLE_HEADER *)addr;
        printf("Entry type %2d ", record_hdr->Type);
        switch (record_hdr->Type) {
            case ACPI_MADT_TYPE_LOCAL_APIC: {
                ACPI_MADT_LOCAL_APIC *apic = (ACPI_MADT_LOCAL_APIC *)record_hdr;
                printf("Local APIC\n");
                printf("  ACPI id: %02x\n", apic->ProcessorId);
                printf("  APIC id: %02x\n", apic->Id);
                printf("  flags: %08x\n", apic->LapicFlags);
                break;
            }
            case ACPI_MADT_TYPE_IO_APIC: {
                ACPI_MADT_IO_APIC *apic = (ACPI_MADT_IO_APIC *)record_hdr;
                printf("IO APIC\n");
                printf("  APIC id: %02x\n", apic->Id);
                printf("  phys: %08x\n", apic->Address);
                printf("  global IRQ base: %08x\n", apic->GlobalIrqBase);
                break;
            }
            case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE: {
                ACPI_MADT_INTERRUPT_OVERRIDE *io =
                        (ACPI_MADT_INTERRUPT_OVERRIDE *)record_hdr;
                printf("Interrupt Source Override\n");
                printf("  bus: %02x (ISA==0)\n", io->Bus);
                printf("  source IRQ: %02x\n", io->SourceIrq);
                printf("  global IRQ: %08x\n", io->GlobalIrq);
                const char *trigger = "";
                const char *polarity = "";
                switch (io->IntiFlags & ACPI_MADT_POLARITY_MASK) {
                    case ACPI_MADT_POLARITY_CONFORMS:
                        polarity = "conforms"; break;
                    case ACPI_MADT_POLARITY_ACTIVE_HIGH:
                        polarity = "high"; break;
                    case ACPI_MADT_POLARITY_RESERVED:
                        polarity = "invalid"; break;
                    case ACPI_MADT_POLARITY_ACTIVE_LOW:
                        polarity = "low"; break;
                }
                switch (io->IntiFlags & ACPI_MADT_TRIGGER_MASK) {
                    case ACPI_MADT_TRIGGER_CONFORMS:
                        trigger = "conforms"; break;
                    case ACPI_MADT_TRIGGER_EDGE:
                        trigger = "edge"; break;
                    case ACPI_MADT_TRIGGER_RESERVED:
                        trigger = "invalid"; break;
                    case ACPI_MADT_TRIGGER_LEVEL:
                        trigger = "level"; break;
                }

                printf("  flags: %04x (trig %s, pol %s)\n",
                       io->IntiFlags, trigger, polarity);
                break;
            }
            case ACPI_MADT_TYPE_LOCAL_APIC_NMI: {
                ACPI_MADT_LOCAL_APIC_NMI *nmi =
                        (ACPI_MADT_LOCAL_APIC_NMI *)record_hdr;
                printf("Local APIC NMI\n");
                printf("  ACPI processor id: %02x\n", nmi->ProcessorId);
                printf("  flags: %04x\n", nmi->IntiFlags);
                printf("  LINTn: %02x\n", nmi->Lint);
                break;
            }
            default:
                printf("Unknown\n");
        }

        addr += record_hdr->Length;
    }
    if (addr != records_end) {
      printf("malformed MADT, last record past the end of the table\n");
    }
}

static void acpi_debug_mcfg(void)
{
    ACPI_TABLE_HEADER *raw_table = NULL;
    ACPI_STATUS status = AcpiGetTable((char *)ACPI_SIG_MCFG, 1, &raw_table);
    if (status != AE_OK) {
        printf("could not find MCFG\n");
        return;
    }
    ACPI_TABLE_MCFG *mcfg = (ACPI_TABLE_MCFG *)raw_table;
    ACPI_MCFG_ALLOCATION *table_start = ((void *)mcfg) + sizeof(*mcfg);
    ACPI_MCFG_ALLOCATION *table_end = ((void *)mcfg) + mcfg->Header.Length;
    ACPI_MCFG_ALLOCATION *table;
    int count = 0;
    if (table_start + 1 > table_end) {
        printf("MCFG has unexpected size\n");
        return;
    }
    for (table = table_start; table < table_end; ++table) {
        printf("Controller %d:\n", count);
        printf("  Physical address: %#016llx\n", table->Address);
        printf("  Segment group: %04x\n", table->PciSegment);
        printf("  Start bus number: %02x\n", table->StartBusNumber);
        printf("  End bus number: %02x\n", table->EndBusNumber);
        ++count;
    }
    if (table != table_end) {
        printf("MCFG has unexpected size\n");
        return;
    }
}

static inline void do_indent(uint level) {
    while (level) {
        printf("  ");
        level--;
    }
}

#define INDENT_PRINTF( ...) do { do_indent(level); printf(__VA_ARGS__); } while (0)

enum print_resource_request {
    CURRENT_RESOURCES,
    POSSIBLE_RESOURCES,
};

static ACPI_STATUS acpi_print_resources(
        ACPI_HANDLE object,
        uint level,
        enum print_resource_request type)
{
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
        return AE_BAD_PARAMETER;;
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
    ACPI_RESOURCE *res = (ACPI_RESOURCE *)entry_addr;
    level += 1;
    while (res->Type != ACPI_RESOURCE_TYPE_END_TAG) {
        INDENT_PRINTF("Entry: ");
        level += 1;
        switch (res->Type) {
            case ACPI_RESOURCE_TYPE_IO: {
                printf("IO\n");
                ACPI_RESOURCE_IO *io = &res->Data.Io;
                INDENT_PRINTF("io_decode: %d\n", io->IoDecode);
                INDENT_PRINTF("alignment: %d\n", io->Alignment);
                INDENT_PRINTF("addrlen: %d\n", io->AddressLength);
                INDENT_PRINTF("address min: %#04x\n", io->Minimum);
                INDENT_PRINTF("address max: %#04x\n", io->Maximum);
                break;
            }
            case ACPI_RESOURCE_TYPE_ADDRESS16: {
                printf("Address16\n");
                ACPI_RESOURCE_ADDRESS16 *a16 = &res->Data.Address16;
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
                ACPI_RESOURCE_ADDRESS32 *a32 = &res->Data.Address32;
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
                ACPI_RESOURCE_IRQ *irq = &res->Data.Irq;
                INDENT_PRINTF("trigger: %s\n", irq->Triggering == ACPI_EDGE_SENSITIVE ? "edge" : "level");
                const char *pol = "invalid";
                switch (irq->Polarity) {
                    case ACPI_ACTIVE_BOTH: pol = "both"; break;
                    case ACPI_ACTIVE_LOW: pol = "low"; break;
                    case ACPI_ACTIVE_HIGH: pol = "high"; break;
                }
                INDENT_PRINTF("polarity: %s\n", pol);
                INDENT_PRINTF("sharable: %d\n", irq->Sharable);
                INDENT_PRINTF("wake_cap: %d\n", irq->WakeCapable);
                for (uint i = 0; i < irq->InterruptCount; ++i) {
                    INDENT_PRINTF("irq #%d: %d\n", i, irq->Interrupts[i]);
                }
                break;
            }
            case ACPI_RESOURCE_TYPE_EXTENDED_IRQ: {
                printf("Extended IRQ\n");
                ACPI_RESOURCE_EXTENDED_IRQ *irq = &res->Data.ExtendedIrq;
                INDENT_PRINTF("produce_consume: %d\n", irq->ProducerConsumer);
                INDENT_PRINTF("trigger: %s\n", irq->Triggering == ACPI_EDGE_SENSITIVE ? "edge" : "level");
                const char *pol = "invalid";
                switch (irq->Polarity) {
                    case ACPI_ACTIVE_BOTH: pol = "both"; break;
                    case ACPI_ACTIVE_LOW: pol = "low"; break;
                    case ACPI_ACTIVE_HIGH: pol = "high"; break;
                }
                INDENT_PRINTF("polarity: %s\n", pol);
                INDENT_PRINTF("sharable: %d\n", irq->Sharable);
                INDENT_PRINTF("wake_cap: %d\n", irq->WakeCapable);
                for (uint i = 0; i < irq->InterruptCount; ++i) {
                    INDENT_PRINTF("irq #%d: %d\n", i, irq->Interrupts[i]);
                }
                break;
            }
            default:
                printf("Unknown (type %u)\n", res->Type);
        }
        level -= 1;

        entry_addr += res->Length;
        res = (ACPI_RESOURCE *)entry_addr;
    }
    level -= 1;

    AcpiOsFree(buffer.Pointer);
    return AE_OK;
}

static ACPI_STATUS acpi_get_pcie_devices_crs(
        ACPI_HANDLE object,
        UINT32 nesting_level,
        void *context,
        void **ret)
{
    printf("Found object %p\n", object);
    return acpi_print_resources(object, 1, CURRENT_RESOURCES);
}

static void acpi_debug_pcie_crs(void)
{
    ACPI_STATUS status = AcpiGetDevices(
            (char*)"PNP0A08",
            acpi_get_pcie_devices_crs,
            NULL,
            NULL);
    if (status != AE_OK) {
        printf("Could not find PCIe root complex\n");
    }
}

static ACPI_STATUS acpi_print_prt(uint level, ACPI_HANDLE object)
{
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
    DEBUG_ASSERT(buffer.Pointer);

    uintptr_t entry_addr = (uintptr_t)buffer.Pointer;
    ACPI_PCI_ROUTING_TABLE *entry;
    for (entry = (ACPI_PCI_ROUTING_TABLE *)entry_addr;
         entry->Length != 0;
         entry_addr += entry->Length, entry = (ACPI_PCI_ROUTING_TABLE *)entry_addr) {

        DEBUG_ASSERT(entry_addr <= (uintptr_t)buffer.Pointer + buffer.Length);

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
        void *context,
        void **ret)
{
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
        uint level = nesting_level;
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


static void acpi_debug_pcie_irq_routing(void)
{
    ACPI_STATUS status = AcpiGetDevices(
            (char*)"PNP0A08",
            acpi_get_pcie_devices_irq,
            NULL,
            NULL);
    if (status != AE_OK) {
        printf("Could not enumerate PRTs\n");
    }
}

static ACPI_STATUS acpi_debug_print_device(
        ACPI_HANDLE object,
        UINT32 nesting_level,
        void *context,
        void **ret)
{
    ACPI_DEVICE_INFO *info = NULL;
    ACPI_STATUS status = AcpiGetObjectInfo(object, &info);
    if (status != AE_OK) {
        if (info) {
            ACPI_FREE(info);
        }
        return status;
    }

    uint level = nesting_level;
    INDENT_PRINTF("%4s\n", (char *)&info->Name);

    ACPI_FREE(info);
    return NO_ERROR;
}

static void acpi_debug_walk_ns(void)
{
    ACPI_STATUS status = AcpiWalkNamespace(
            ACPI_TYPE_DEVICE,
            ACPI_ROOT_OBJECT,
            INT_MAX,
            acpi_debug_print_device,
            NULL,
            NULL,
            NULL);
    if (status != AE_OK) {
        printf("Failed to walk namespace\n");
    }
}

static int spy_thread(void* arg) {
    port_t port;
    if (port_open(POWER_BUTTON_PORT, NULL, &port) < 0) {
        printf("acpi port open failed\n");
    }

    port_result_t pr;
    for(;;) {
        if (port_read(port, INFINITE_TIME, &pr) < 0) {
            break;
        }
        printf("[acpi: pwr-sw %s]\n", pr.packet.value);
    }

    printf("acpi spy thread terminated\n");
    return 0; 
}

static void acpi_debug_hook_all_events(void)
{
    thread_t* th = thread_create("acpi-spy", spy_thread, NULL,
                                 DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
    thread_resume(th);
    printf("listening for ACPI events\n");
}

#undef INDENT_PRINTF

static int cmd_acpi(int argc, const cmd_args *argv)
{
    if (argc < 2) {
notenoughargs:
        printf("not enough arguments\n");
usage:
        printf("usage:\n");
        printf("%s madt\n", argv[0].str);
        printf("%s mcfg\n", argv[0].str);
        printf("%s pcie-crs\n", argv[0].str);
        printf("%s pcie-irq\n", argv[0].str);
        printf("%s walk-ns\n", argv[0].str);
        printf("%s spy\n", argv[0].str);
        return ERR_GENERIC;
    }

    if (!strcmp(argv[1].str, "madt")) {
        acpi_debug_madt();
    } else if (!strcmp(argv[1].str, "mcfg")) {
        acpi_debug_mcfg();
    } else if (!strcmp(argv[1].str, "pcie-crs")) {
        acpi_debug_pcie_crs();
    } else if (!strcmp(argv[1].str, "pcie-irq")) {
        acpi_debug_pcie_irq_routing();
    } else if (!strcmp(argv[1].str, "walk-ns")) {
        acpi_debug_walk_ns();
    } else if (!strcmp(argv[1].str, "spy")) {
        acpi_debug_hook_all_events();
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return NO_ERROR;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("acpi", "acpi commands", &cmd_acpi)
#endif
STATIC_COMMAND_END(acpi);
