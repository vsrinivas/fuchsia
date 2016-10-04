// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifdef WITH_LIB_CONSOLE

#include <debug.h>
#include <dev/pcie.h>
#include <err.h>
#include <inttypes.h>
#include <lib/console.h>
#include <string.h>

#include "pcie_priv.h"

/* Class code/Subclass code definitions taken from
 * http://wiki.osdev.org/Pci#Class_Codes */
typedef struct {
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prof_if_start;
    uint8_t prof_if_end;
    const char* desc;
} pci_dev_type_lut_entry_t;

typedef struct lspci_params {
    bool verbose;
    uint base_level;
    uint indent_level;
    uint bus_id;
    uint dev_id;
    uint func_id;
    uint cfg_dump_amt;
    bool force_dump_cfg;
    uint found;
} lspci_params_t;

#define WILDCARD_ID (0xFFFFFFFF)

#define LUT_ENTRY(_class, _subclass, _pif_start, _pif_end, _desc) { \
    .class_code    = _class,                                        \
    .subclass      = _subclass,                                     \
    .prof_if_start = _pif_start,                                    \
    .prof_if_end   = _pif_end,                                      \
    .desc          = _desc,                                         \
}

#define LUT_ENTRY_ONE_PIF(_class, _subclass, _pif, _desc) \
    LUT_ENTRY(_class, _subclass, _pif, _pif, _desc)

#define LUT_ENTRY_ALL_PIF(_class, _subclass, _desc) \
    LUT_ENTRY(_class, _subclass, 0x00, 0xFF, _desc)

static const pci_dev_type_lut_entry_t PCI_DEV_TYPE_LUT[] = {
    LUT_ENTRY_ONE_PIF(0x00, 0x00, 0x00, "Any device except for VGA-Compatible devices"),
    LUT_ENTRY_ONE_PIF(0x00, 0x01, 0x00, "VGA-Compatible Device"),
    LUT_ENTRY_ONE_PIF(0x01, 0x00, 0x00, "SCSI Bus Controller"),
    LUT_ENTRY_ALL_PIF(0x01, 0x01,       "IDE Controller"),
    LUT_ENTRY_ONE_PIF(0x01, 0x02, 0x00, "Floppy Disk Controller"),
    LUT_ENTRY_ONE_PIF(0x01, 0x03, 0x00, "IPI Bus Controller"),
    LUT_ENTRY_ONE_PIF(0x01, 0x04, 0x00, "RAID Controller"),
    LUT_ENTRY_ONE_PIF(0x01, 0x05, 0x20, "ATA Controller (Single DMA)"),
    LUT_ENTRY_ONE_PIF(0x01, 0x05, 0x30, "ATA Controller (Chained DMA)"),
    LUT_ENTRY_ONE_PIF(0x01, 0x06, 0x00, "Serial ATA (Vendor Specific Interface)"),
    LUT_ENTRY_ONE_PIF(0x01, 0x06, 0x01, "Serial ATA (AHCI 1.0)"),
    LUT_ENTRY_ONE_PIF(0x01, 0x07, 0x00, "Serial Attached SCSI (SAS)"),
    LUT_ENTRY_ONE_PIF(0x01, 0x80, 0x00, "Other Mass Storage Controller"),
    LUT_ENTRY_ONE_PIF(0x02, 0x00, 0x00, "Ethernet Controller"),
    LUT_ENTRY_ONE_PIF(0x02, 0x01, 0x00, "Token Ring Controller"),
    LUT_ENTRY_ONE_PIF(0x02, 0x02, 0x00, "FDDI Controller"),
    LUT_ENTRY_ONE_PIF(0x02, 0x03, 0x00, "ATM Controller"),
    LUT_ENTRY_ONE_PIF(0x02, 0x04, 0x00, "ISDN Controller"),
    LUT_ENTRY_ONE_PIF(0x02, 0x05, 0x00, "WorldFip Controller"),
    LUT_ENTRY_ALL_PIF(0x02, 0x06,       "PICMG 2.14 Multi Computing"),
    LUT_ENTRY_ONE_PIF(0x02, 0x80, 0x00, "Other Network Controller"),
    LUT_ENTRY_ONE_PIF(0x03, 0x00, 0x00, "VGA-Compatible Controller"),
    LUT_ENTRY_ONE_PIF(0x03, 0x00, 0x01, "8512-Compatible Controller"),
    LUT_ENTRY_ONE_PIF(0x03, 0x01, 0x00, "XGA Controller"),
    LUT_ENTRY_ONE_PIF(0x03, 0x02, 0x00, "3D Controller (Not VGA-Compatible)"),
    LUT_ENTRY_ONE_PIF(0x03, 0x80, 0x00, "Other Display Controller"),
    LUT_ENTRY_ONE_PIF(0x04, 0x00, 0x00, "Video Device"),
    LUT_ENTRY_ONE_PIF(0x04, 0x01, 0x00, "Audio Device"),
    LUT_ENTRY_ONE_PIF(0x04, 0x02, 0x00, "Computer Telephony Device"),
    LUT_ENTRY_ONE_PIF(0x04, 0x80, 0x00, "Other Multimedia Device"),
    LUT_ENTRY_ONE_PIF(0x05, 0x00, 0x00, "RAM Controller"),
    LUT_ENTRY_ONE_PIF(0x05, 0x01, 0x00, "Flash Controller"),
    LUT_ENTRY_ONE_PIF(0x05, 0x80, 0x00, "Other Memory Controller"),
    LUT_ENTRY_ONE_PIF(0x06, 0x00, 0x00, "Host Bridge"),
    LUT_ENTRY_ONE_PIF(0x06, 0x01, 0x00, "ISA Bridge"),
    LUT_ENTRY_ONE_PIF(0x06, 0x02, 0x00, "EISA Bridge"),
    LUT_ENTRY_ONE_PIF(0x06, 0x03, 0x00, "MCA Bridge"),
    LUT_ENTRY_ONE_PIF(0x06, 0x04, 0x00, "PCI-to-PCI Bridge"),
    LUT_ENTRY_ONE_PIF(0x06, 0x04, 0x01, "PCI-to-PCI Bridge (Subtractive Decode)"),
    LUT_ENTRY_ONE_PIF(0x06, 0x05, 0x00, "PCMCIA Bridge"),
    LUT_ENTRY_ONE_PIF(0x06, 0x06, 0x00, "NuBus Bridge"),
    LUT_ENTRY_ONE_PIF(0x06, 0x07, 0x00, "CardBus Bridge"),
    LUT_ENTRY_ALL_PIF(0x06, 0x08,       "RACEway Bridge"),
    LUT_ENTRY_ONE_PIF(0x06, 0x09, 0x40, "PCI-to-PCI Bridge (Semi-Transparent, Primary)"),
    LUT_ENTRY_ONE_PIF(0x06, 0x09, 0x80, "PCI-to-PCI Bridge (Semi-Transparent, Secondary)"),
    LUT_ENTRY_ONE_PIF(0x06, 0x0A, 0x00, "InfiniBrand-to-PCI Host Bridge"),
    LUT_ENTRY_ONE_PIF(0x06, 0x80, 0x00, "Other Bridge Device"),
    LUT_ENTRY_ONE_PIF(0x07, 0x00, 0x00, "Generic XT-Compatible Serial Controller"),
    LUT_ENTRY_ONE_PIF(0x07, 0x00, 0x01, "16450-Compatible Serial Controller"),
    LUT_ENTRY_ONE_PIF(0x07, 0x00, 0x02, "16550-Compatible Serial Controller"),
    LUT_ENTRY_ONE_PIF(0x07, 0x00, 0x03, "16650-Compatible Serial Controller"),
    LUT_ENTRY_ONE_PIF(0x07, 0x00, 0x04, "16750-Compatible Serial Controller"),
    LUT_ENTRY_ONE_PIF(0x07, 0x00, 0x05, "16850-Compatible Serial Controller"),
    LUT_ENTRY_ONE_PIF(0x07, 0x00, 0x06, "16950-Compatible Serial Controller"),
    LUT_ENTRY_ONE_PIF(0x07, 0x01, 0x00, "Parallel Port"),
    LUT_ENTRY_ONE_PIF(0x07, 0x01, 0x01, "Bi-Directional Parallel Port"),
    LUT_ENTRY_ONE_PIF(0x07, 0x01, 0x02, "ECP 1.X Compliant Parallel Port"),
    LUT_ENTRY_ONE_PIF(0x07, 0x01, 0x03, "IEEE 1284 Controller"),
    LUT_ENTRY_ONE_PIF(0x07, 0x01, 0xFE, "IEEE 1284 Target Device"),
    LUT_ENTRY_ONE_PIF(0x07, 0x02, 0x00, "Multiport Serial Controller"),
    LUT_ENTRY_ONE_PIF(0x07, 0x03, 0x00, "Generic Modem"),
    LUT_ENTRY_ONE_PIF(0x07, 0x03, 0x01, "Hayes Compatible Modem (16450-Compatible Interface)"),
    LUT_ENTRY_ONE_PIF(0x07, 0x03, 0x02, "Hayes Compatible Modem (16550-Compatible Interface)"),
    LUT_ENTRY_ONE_PIF(0x07, 0x03, 0x03, "Hayes Compatible Modem (16650-Compatible Interface)"),
    LUT_ENTRY_ONE_PIF(0x07, 0x03, 0x04, "Hayes Compatible Modem (16750-Compatible Interface)"),
    LUT_ENTRY_ONE_PIF(0x07, 0x04, 0x00, "IEEE 488.1/2 (GPIB) Controller"),
    LUT_ENTRY_ONE_PIF(0x07, 0x05, 0x00, "Smart Card"),
    LUT_ENTRY_ONE_PIF(0x07, 0x80, 0x00, "Other Communications Device"),
    LUT_ENTRY_ONE_PIF(0x08, 0x00, 0x00, "Generic 8259 PIC"),
    LUT_ENTRY_ONE_PIF(0x08, 0x00, 0x01, "ISA PIC"),
    LUT_ENTRY_ONE_PIF(0x08, 0x00, 0x02, "EISA PIC"),
    LUT_ENTRY_ONE_PIF(0x08, 0x00, 0x10, "I/O APIC Interrupt Controller"),
    LUT_ENTRY_ONE_PIF(0x08, 0x00, 0x20, "I/O(x) APIC Interrupt Controller"),
    LUT_ENTRY_ONE_PIF(0x08, 0x01, 0x00, "Generic 8237 DMA Controller"),
    LUT_ENTRY_ONE_PIF(0x08, 0x01, 0x01, "ISA DMA Controller"),
    LUT_ENTRY_ONE_PIF(0x08, 0x01, 0x02, "EISA DMA Controller"),
    LUT_ENTRY_ONE_PIF(0x08, 0x02, 0x00, "Generic 8254 System Timer"),
    LUT_ENTRY_ONE_PIF(0x08, 0x02, 0x01, "ISA System Timer"),
    LUT_ENTRY_ONE_PIF(0x08, 0x02, 0x02, "EISA System Timer"),
    LUT_ENTRY_ONE_PIF(0x08, 0x03, 0x00, "Generic RTC Controller"),
    LUT_ENTRY_ONE_PIF(0x08, 0x03, 0x01, "ISA RTC Controller"),
    LUT_ENTRY_ONE_PIF(0x08, 0x04, 0x00, "Generic PCI Hot-Plug Controller"),
    LUT_ENTRY_ONE_PIF(0x08, 0x80, 0x00, "Other System Peripheral"),
    LUT_ENTRY_ONE_PIF(0x09, 0x00, 0x00, "Keyboard Controller"),
    LUT_ENTRY_ONE_PIF(0x09, 0x01, 0x00, "Digitizer"),
    LUT_ENTRY_ONE_PIF(0x09, 0x02, 0x00, "Mouse Controller"),
    LUT_ENTRY_ONE_PIF(0x09, 0x03, 0x00, "Scanner Controller"),
    LUT_ENTRY_ONE_PIF(0x09, 0x04, 0x00, "Gameport Controller (Generic)"),
    LUT_ENTRY_ONE_PIF(0x09, 0x04, 0x10, "Gameport Contrlller (Legacy)"),
    LUT_ENTRY_ONE_PIF(0x09, 0x80, 0x00, "Other Input Controller"),
    LUT_ENTRY_ONE_PIF(0x0a, 0x00, 0x00, "Generic Docking Station"),
    LUT_ENTRY_ONE_PIF(0x0a, 0x80, 0x00, "Other Docking Station"),
    LUT_ENTRY_ONE_PIF(0x0b, 0x00, 0x00, "386 Processor"),
    LUT_ENTRY_ONE_PIF(0x0b, 0x01, 0x00, "486 Processor"),
    LUT_ENTRY_ONE_PIF(0x0b, 0x02, 0x00, "Pentium Processor"),
    LUT_ENTRY_ONE_PIF(0x0b, 0x10, 0x00, "Alpha Processor"),
    LUT_ENTRY_ONE_PIF(0x0b, 0x20, 0x00, "PowerPC Processor"),
    LUT_ENTRY_ONE_PIF(0x0b, 0x30, 0x00, "MIPS Processor"),
    LUT_ENTRY_ONE_PIF(0x0b, 0x40, 0x00, "Co-Processor"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x00, 0x00, "IEEE 1394 Controller (FireWire)"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x00, 0x10, "IEEE 1394 Controller (1394 OpenHCI Spec)"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x01, 0x00, "ACCESS.bus"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x02, 0x00, "SSA"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x03, 0x00, "USB (Universal Host Controller Spec)"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x03, 0x10, "USB (Open Host Controller Spec)"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x03, 0x20, "USB2 Host Controller (Intel EHCI)"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x03, 0x30, "USB3 XHCI Controller"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x03, 0x80, "Unspecified USB Controller"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x03, 0xFE, "USB (Not Host Controller)"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x04, 0x00, "Fibre Channel"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x05, 0x00, "SMBus"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x06, 0x00, "InfiniBand"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x07, 0x00, "IPMI SMIC Interface"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x07, 0x01, "IPMI Kybd Controller Style Interface"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x07, 0x02, "IPMI Block Transfer Interface"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x08, 0x00, "SERCOS Interface Standard (IEC 61491)"),
    LUT_ENTRY_ONE_PIF(0x0c, 0x09, 0x00, "CANbus"),
    LUT_ENTRY_ONE_PIF(0x0d, 0x00, 0x00, "iRDA Compatible Controller"),
    LUT_ENTRY_ONE_PIF(0x0d, 0x01, 0x00, "Consumer IR Controller"),
    LUT_ENTRY_ONE_PIF(0x0d, 0x10, 0x00, "RF Controller"),
    LUT_ENTRY_ONE_PIF(0x0d, 0x11, 0x00, "Bluetooth Controller"),
    LUT_ENTRY_ONE_PIF(0x0d, 0x12, 0x00, "Broadband Controller"),
    LUT_ENTRY_ONE_PIF(0x0d, 0x20, 0x00, "Ethernet Controller (802.11a)"),
    LUT_ENTRY_ONE_PIF(0x0d, 0x21, 0x00, "Ethernet Controller (802.11b)"),
    LUT_ENTRY_ONE_PIF(0x0d, 0x80, 0x00, "Other Wireless Controller"),
    LUT_ENTRY        (0x0e, 0x00, 0x01, 0xFF, "I20 Architecture"),
    LUT_ENTRY_ONE_PIF(0x0e, 0x00, 0x00, "Message FIFO"),
    LUT_ENTRY_ONE_PIF(0x0f, 0x01, 0x00, "TV Controller"),
    LUT_ENTRY_ONE_PIF(0x0f, 0x02, 0x00, "Audio Controller"),
    LUT_ENTRY_ONE_PIF(0x0f, 0x03, 0x00, "Voice Controller"),
    LUT_ENTRY_ONE_PIF(0x0f, 0x04, 0x00, "Data Controller"),
    LUT_ENTRY_ONE_PIF(0x10, 0x00, 0x00, "Network and Computing Encrpytion/Decryption"),
    LUT_ENTRY_ONE_PIF(0x10, 0x10, 0x00, "Entertainment Encryption/Decryption"),
    LUT_ENTRY_ONE_PIF(0x10, 0x80, 0x00, "Other Encryption/Decryption"),
    LUT_ENTRY_ONE_PIF(0x11, 0x00, 0x00, "DPIO Modules"),
    LUT_ENTRY_ONE_PIF(0x11, 0x01, 0x00, "Performance Counters"),
    LUT_ENTRY_ONE_PIF(0x11, 0x10, 0x00, "Communications Syncrhonization"),
    LUT_ENTRY_ONE_PIF(0x11, 0x20, 0x00, "Management Card"),
    LUT_ENTRY_ONE_PIF(0x11, 0x80, 0x00, "Other Data Acquisition/Signal Processing Controller"),
};

#undef LUT_ENTRY
#undef LUT_ENTRY_ONE_PIF
#undef LUT_ENTRY_ALL_PIF

static const char* pci_class_code_to_string(uint8_t class_code)
{
    switch (class_code) {
        case 0x00: return "Pre-Class Code Device";
        case 0x01: return "Mass Storage Controller";
        case 0x02: return "Network Controller";
        case 0x03: return "Display Controller";
        case 0x04: return "Multimedia Controller";
        case 0x05: return "Memory Controller";
        case 0x06: return "Bridge Device";
        case 0x07: return "Simple Communication Controller";
        case 0x08: return "Base System Peripheral";
        case 0x09: return "Input Device";
        case 0x0A: return "Docking Station";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial Bus Controller";
        case 0x0D: return "Wireless Controller";
        case 0x0E: return "Intelligent I/O Controller";
        case 0x0F: return "Satellite Communication Controller";
        case 0x10: return "Encryption/Decryption Controller";
        case 0x11: return "Data Acquisition or Signal Processing Controller";
        case 0xFF: return "Vendor";
        default:   return "<Unknown>";
    }
}

static const char* pci_device_type(const pcie_device_state_t& dev)
{
    // TODO(johngro): It might be a good idea, some day, to make this something
    // better than an O(n) search.

    // If this is a PCIe style bridge with a specific device type spelled out in
    // its PCI Express Capabilities structure, use that to provide the type
    // string.
    switch (dev.pcie_caps.devtype) {
        case PCIE_DEVTYPE_RC_ROOT_PORT:           return "PCIe Root Port";
        case PCIE_DEVTYPE_SWITCH_UPSTREAM_PORT:   return "PCIe Upstream Switch Port";
        case PCIE_DEVTYPE_SWITCH_DOWNSTREAM_PORT: return "PCIe Downstream Switch Port";
        case PCIE_DEVTYPE_PCIE_TO_PCI_BRIDGE:     return "PCIe-to-PCI Bridge";
        case PCIE_DEVTYPE_PCI_TO_PCIE_BRIDGE:     return "PCI-to-PCIe Bridge";
        default: break;
    }

    for (size_t i = 0; i < countof(PCI_DEV_TYPE_LUT); ++i) {
        const pci_dev_type_lut_entry_t* entry = PCI_DEV_TYPE_LUT + i;

        if ((dev.class_id == entry->class_code)    &&
            (dev.subclass == entry->subclass)      &&
            (dev.prog_if  >= entry->prof_if_start) &&
            (dev.prog_if  <= entry->prof_if_end))
            return entry->desc;
    }

    return pci_class_code_to_string(dev.class_id);
}

static void do_lspci_indent(uint level) {
    while (level--)
        printf("  ");
}
#define LSPCI_PRINTF(_fmt, ...)                \
    do {                                       \
        do_lspci_indent(params->indent_level); \
        printf(_fmt, ##__VA_ARGS__);           \
    } while (0)

static void dump_pcie_hdr(const pcie_device_state_t& dev, lspci_params_t* params)
{
    DEBUG_ASSERT(params);
    LSPCI_PRINTF("[%02x:%02x.%01x] - VID 0x%04x DID 0x%04x :: %s",
                 dev.bus_id, dev.dev_id, dev.func_id,
                 dev.vendor_id, dev.device_id,
                 pci_device_type(dev));

    if (dev.driver)
        printf(" [driver = \"%s\"]", pcie_driver_name(dev.driver));

    printf("\n");
}

static void dump_pcie_bars(const pcie_device_state_t& dev,
                           lspci_params_t* params,
                           uint bar_count)
{
    pci_config_t* cfg = &dev.cfg->base;

    DEBUG_ASSERT(bar_count <= countof(cfg->base_addresses));
    DEBUG_ASSERT(bar_count <= countof(dev.bars));
    for (uint i = 0; i < bar_count; ++i) {
        DEBUG_ASSERT(i < bar_count);

        LSPCI_PRINTF("Base Addr[%u]      : 0x%08x", i, pcie_read32(&cfg->base_addresses[i]));

        const pcie_bar_info_t* info = pcie_get_bar_info(dev, i);
        if (!info) {
            printf("\n");
            continue;
        }

        printf(" :: paddr %#" PRIx64 " size %#" PRIx64 "%s%s %s\n",
                info->bus_addr,
                info->size,
                info->is_prefetchable ? " prefetchable" : "",
                info->is_mmio ? (info->is_64bit ? " 64-bit" : " 32-bit") : "",
                info->is_mmio ? "MMIO" : "PIO");
    }
}

static void dump_pcie_common(const pcie_device_state_t& dev, lspci_params_t* params)
{
    pci_config_t* cfg = &dev.cfg->base;
    uint8_t base_class = pcie_read8(&cfg->base_class);

    LSPCI_PRINTF("Command           : 0x%04x\n",    pcie_read16(&cfg->command));
    LSPCI_PRINTF("Status            : 0x%04x\n",    pcie_read16(&cfg->status));
    LSPCI_PRINTF("Rev ID            : 0x%02x\n",    pcie_read8(&cfg->revision_id_0));
    LSPCI_PRINTF("Prog Iface        : 0x%02x\n",    pcie_read8(&cfg->program_interface));
    LSPCI_PRINTF("Sub Class         : 0x%02x\n",    pcie_read8(&cfg->sub_class));
    LSPCI_PRINTF("Base Class        : 0x%02x %s\n", base_class,
                                                    pci_class_code_to_string(base_class));
    LSPCI_PRINTF("Cache Line Sz     : 0x%02x\n",    pcie_read8(&cfg->cache_line_size));
    LSPCI_PRINTF("Latency Timer     : 0x%02x\n",    pcie_read8(&cfg->latency_timer));
    LSPCI_PRINTF("Header Type       : 0x%02x\n",    pcie_read8(&cfg->header_type));
    LSPCI_PRINTF("BIST              : 0x%02x\n",    pcie_read8(&cfg->bist));
}

static void dump_pcie_standard(const pcie_device_state_t& dev, lspci_params_t* params)
{
    pci_config_t* cfg = &dev.cfg->base;
    LSPCI_PRINTF("Cardbus CIS       : 0x%08x\n", pcie_read32(&cfg->cardbus_cis_ptr));
    LSPCI_PRINTF("Subsystem VID     : 0x%04x\n", pcie_read16(&cfg->subsystem_vendor_id));
    LSPCI_PRINTF("Subsystem ID      : 0x%04x\n", pcie_read16(&cfg->subsystem_id));
    LSPCI_PRINTF("Exp ROM addr      : 0x%08x\n", pcie_read32(&cfg->expansion_rom_address));
    LSPCI_PRINTF("Cap Ptr           : 0x%02x\n", pcie_read8(&cfg->capabilities_ptr));
    LSPCI_PRINTF("IRQ line          : 0x%02x\n", pcie_read8(&cfg->interrupt_line));
    LSPCI_PRINTF("IRQ pin           : 0x%02x\n", pcie_read8(&cfg->interrupt_pin));
    LSPCI_PRINTF("Min Grant         : 0x%02x\n", pcie_read8(&cfg->min_grant));
    LSPCI_PRINTF("Max Latency       : 0x%02x\n", pcie_read8(&cfg->max_latency));
}

static void dump_pcie_bridge(const pcie_device_state_t& dev, lspci_params_t* params)
{
    pci_to_pci_bridge_config_t* bcfg = (pci_to_pci_bridge_config_t*)(&dev.cfg->base);

    LSPCI_PRINTF("P. Bus ID         : 0x%02x\n", pcie_read8(&bcfg->primary_bus_id));
    LSPCI_PRINTF("S. Bus Range      : [0x%02x, 0x%02x]\n",
                                                 pcie_read8(&bcfg->secondary_bus_id),
                                                 pcie_read8(&bcfg->subordinate_bus_id));
    LSPCI_PRINTF("S. Latency Timer  : 0x%02x\n", pcie_read8(&bcfg->secondary_latency_timer));
    LSPCI_PRINTF("IO Base           : 0x%02x\n", pcie_read8(&bcfg->io_base));
    LSPCI_PRINTF("IO Base Upper     : 0x%04x\n", pcie_read16(&bcfg->io_base_upper));
    LSPCI_PRINTF("IO Limit          : 0x%02x\n", pcie_read8(&bcfg->io_limit));
    LSPCI_PRINTF("IO Limit Upper    : 0x%04x\n", pcie_read16(&bcfg->io_limit_upper));
    LSPCI_PRINTF("Secondary Status  : 0x%04x\n", pcie_read16(&bcfg->secondary_status));
    LSPCI_PRINTF("Memory Limit      : 0x%04x\n", pcie_read16(&bcfg->memory_limit));
    LSPCI_PRINTF("Memory Base       : 0x%04x\n", pcie_read16(&bcfg->memory_base));
    LSPCI_PRINTF("PFMem Base        : 0x%04x\n", pcie_read16(&bcfg->prefetchable_memory_base));
    LSPCI_PRINTF("PFMem Base Upper  : 0x%08x\n", pcie_read32(&bcfg->prefetchable_memory_base_upper));
    LSPCI_PRINTF("PFMem Limit       : 0x%04x\n", pcie_read16(&bcfg->prefetchable_memory_limit));
    LSPCI_PRINTF("PFMem Limit Upper : 0x%08x\n", pcie_read32(&bcfg->prefetchable_memory_limit_upper));
    LSPCI_PRINTF("Capabilities Ptr  : 0x%02x\n", pcie_read8(&bcfg->capabilities_ptr));
    LSPCI_PRINTF("Exp ROM Address   : 0x%08x\n", pcie_read32(&bcfg->expansion_rom_address));
    LSPCI_PRINTF("Interrupt Line    : 0x%02x\n", pcie_read8(&bcfg->interrupt_line));
    LSPCI_PRINTF("Interrupt Pin     : 0x%02x\n", pcie_read8(&bcfg->interrupt_pin));
    LSPCI_PRINTF("Bridge Control    : 0x%04x\n", pcie_read16(&bcfg->bridge_control));
}

static void dump_pcie_raw_config(uint amt, void* kvaddr, uint64_t phys)
{
    printf("%u bytes of raw config (kvaddr %p; phys %#" PRIx64 ")\n",
           amt, kvaddr, phys);
    hexdump8(kvaddr, amt);
}

static bool dump_pcie_device(const mxtl::RefPtr<pcie_device_state_t>& dev, void* ctx, uint level)
{
    DEBUG_ASSERT(dev && ctx);
    lspci_params_t* params = (lspci_params_t*)ctx;
    bool match;

    /* Grab the device's lock so it cannot be unplugged out from under us while
     * we print details. */
    mutex_acquire(&dev->dev_lock);

    /* If the device has already been unplugged, just skip it */
    if (!dev->plugged_in)
        goto finished;

    match = (((params->bus_id  == WILDCARD_ID) || (params->bus_id  == dev->bus_id)) &&
             ((params->dev_id  == WILDCARD_ID) || (params->dev_id  == dev->dev_id)) &&
             ((params->func_id == WILDCARD_ID) || (params->func_id == dev->func_id)));
    if (!match)
        goto finished;

    if (!params->found && (params->bus_id != WILDCARD_ID)) {
        params->base_level = level;
    } else {
        DEBUG_ASSERT(!params->base_level);
    }

    params->found++;

    DEBUG_ASSERT(level >= params->base_level);
    params->indent_level = params->verbose ? 0 : level - params->base_level;

    /* Dump the header */
    dump_pcie_hdr(*dev, params);

    /* Only dump details if we are in verbose mode and this device matches our
     * filter */
    if (params->verbose) {
        params->indent_level += 2;

        dump_pcie_common(*dev, params);

        uint8_t header_type = pcie_read8(&dev->cfg->base.header_type) & PCI_HEADER_TYPE_MASK;
        switch (header_type) {
        case PCI_HEADER_TYPE_STANDARD:
            dump_pcie_bars(*dev, params, 6);
            dump_pcie_standard(*dev, params);
            break;

        case PCI_HEADER_TYPE_PCI_BRIDGE:
            dump_pcie_bars(*dev, params, 2);
            dump_pcie_bridge(*dev, params);
            break;

        case PCI_HEADER_TYPE_CARD_BUS:
            printf("TODO : Implemnt CardBus Config header register dump\n");
            break;

        default:
            printf("Unknown Header Type (0x%02x)\n", header_type);
            break;
        }

        params->indent_level -= 2;
    }

    if (params->cfg_dump_amt)
        dump_pcie_raw_config(params->cfg_dump_amt, dev->cfg, (uint64_t)dev->cfg_phys);

finished:
    mutex_release(&dev->dev_lock);
    return true;
}

static int cmd_lspci(int argc, const cmd_args *argv)
{
    lspci_params_t params;
    uint filter_ndx = 0;

    memset(&params, 0, sizeof(params));
    params.bus_id  = WILDCARD_ID;
    params.dev_id  = WILDCARD_ID;
    params.func_id = WILDCARD_ID;

    for (int i = 1; i < argc; ++i) {
        bool confused = false;

        if (argv[i].str[0] == '-') {
            const char* c = argv[i].str + 1;
            if (!(*c))
                confused = true;

            while (!confused && *c) {
                switch (*c) {
                    case 'f':
                        if (params.cfg_dump_amt < PCIE_BASE_CONFIG_SIZE)
                            params.cfg_dump_amt = PCIE_BASE_CONFIG_SIZE;
                        params.force_dump_cfg = true;
                        break;

                    case 'e':
                        if (params.cfg_dump_amt < PCIE_EXTENDED_CONFIG_SIZE)
                            params.cfg_dump_amt = PCIE_EXTENDED_CONFIG_SIZE;
                        // deliberate fall-thru

                    case 'c':
                        if (params.cfg_dump_amt < PCIE_BASE_CONFIG_SIZE)
                            params.cfg_dump_amt = PCIE_BASE_CONFIG_SIZE;
                        // deliberate fall-thru

                    case 'l':
                        params.verbose = true;
                        break;

                    default:
                        confused = true;
                        break;
                }

                c++;
            }
        } else {
            switch (filter_ndx) {
                case 0:
                    params.bus_id = static_cast<uint>(argv[i].i);
                    if (params.bus_id >= PCIE_MAX_BUSSES)
                        confused = true;
                    break;

                case 1:
                    params.dev_id = static_cast<uint>(argv[i].i);
                    if (params.dev_id >= PCIE_MAX_DEVICES_PER_BUS)
                        confused = true;
                    break;

                case 2:
                    params.func_id = static_cast<uint>(argv[i].i);
                    if (params.func_id >= PCIE_MAX_FUNCTIONS_PER_DEVICE)
                        confused = true;
                    break;

                default:
                    confused = true;
                    break;
            }

            filter_ndx++;
        }

        if (confused) {
            printf("usage: %s [-t] [-l] [<bus_id>] [<dev_id>] [<func_id>]\n", argv[0].str);
            printf("       -l : Be verbose when dumping info about discovered devices.\n");
            printf("       -c : Dump raw standard config (implies -l)\n");
            printf("       -e : Dump raw extended config (implies -l -c)\n");
            printf("       -f : Force dump at least standard config, even if the device didn't "
                               "enumerate (requires a full BDF address)\n");
            return NO_ERROR;
        }
    }

    pcie_bus_driver_state_t* bus_drv = pcie_get_bus_driver_state();
    pcie_foreach_device(bus_drv, dump_pcie_device, &params);

    if (!params.found && params.force_dump_cfg &&
        (params.bus_id  != WILDCARD_ID) &&
        (params.dev_id  != WILDCARD_ID) &&
        (params.func_id != WILDCARD_ID)) {
        pcie_config_t* cfg;
        uint64_t cfg_phys;

        cfg = pcie_get_config(bus_drv, &cfg_phys, params.bus_id, params.dev_id, params.func_id);
        if (!cfg) {
            printf("Config space for %02x:%02x.%01x not mapped by bus driver!\n",
                   params.bus_id, params.dev_id, params.func_id);
        } else {
            dump_pcie_raw_config(params.cfg_dump_amt, cfg, cfg_phys);
        }
    } else {
        printf("PCIe scan discovered %u device%s\n", params.found, (params.found == 1) ? "" : "s");
    }

    return NO_ERROR;
}

static int cmd_pciunplug(int argc, const cmd_args *argv)
{
    bool confused = false;
    uint bus_id, dev_id, func_id;

    if (argc == 4) {
        bus_id  = static_cast<uint>(argv[1].i);
        dev_id  = static_cast<uint>(argv[2].i);
        func_id = static_cast<uint>(argv[3].i);

        if ((bus_id  >= PCIE_MAX_BUSSES) ||
            (dev_id  >= PCIE_MAX_DEVICES_PER_BUS) ||
            (func_id >= PCIE_MAX_FUNCTIONS_PER_DEVICE))
            confused = true;
    } else {
        confused = true;
    }

    if (confused) {
        printf("usage: %s <bus_id> <dev_id> <func_id>\n", argv[0].str);
        return NO_ERROR;
    }

    mxtl::RefPtr<pcie_device_state_t> dev = pcie_get_refed_device(pcie_get_bus_driver_state(),
                                                                  bus_id, dev_id, func_id);

    if (!dev) {
        printf("Failed to find PCI device %02x:%02x.%01x\n", bus_id, dev_id, func_id);
    } else {
        printf("Shutting down and unplugging PCI device %02x:%02x.%x (%s)...\n",
                bus_id, dev_id, func_id, pcie_driver_name(dev->driver));
        pcie_shutdown_device(dev);
        pcie_unplug_device(dev);
        dev = nullptr;
        printf("done\n");
    }

    return NO_ERROR;
}

static int cmd_pcireset(int argc, const cmd_args *argv)
{
    bool confused = false;
    uint bus_id, dev_id, func_id;

    if (argc == 4) {
        bus_id  = static_cast<uint>(argv[1].i);
        dev_id  = static_cast<uint>(argv[2].i);
        func_id = static_cast<uint>(argv[3].i);

        if ((bus_id  >= PCIE_MAX_BUSSES) ||
            (dev_id  >= PCIE_MAX_DEVICES_PER_BUS) ||
            (func_id >= PCIE_MAX_FUNCTIONS_PER_DEVICE))
            confused = true;
    } else {
        confused = true;
    }

    if (confused) {
        printf("usage: %s <bus_id> <dev_id> <func_id>\n", argv[0].str);
        return NO_ERROR;
    }

    mxtl::RefPtr<pcie_device_state_t> dev = pcie_get_refed_device(pcie_get_bus_driver_state(),
                                                                  bus_id, dev_id, func_id);

    if (!dev) {
        printf("Failed to find PCI device %02x:%02x.%01x\n", bus_id, dev_id, func_id);
    } else {
        printf("Attempting reset of device %02x:%02x.%01x...\n", bus_id, dev_id, func_id);
        status_t res = pcie_do_function_level_reset(dev);
        dev = nullptr;
        if (res != NO_ERROR)
            printf("Reset attempt failed (res = %d).\n", res);
        else
            printf("Success, device %02x:%02x.%01x has been reset.\n", bus_id, dev_id, func_id);
    }

    return NO_ERROR;
}

static int cmd_pcirescan(int argc, const cmd_args *argv)
{
    pcie_scan_and_start_devices(pcie_get_bus_driver_state());
    return NO_ERROR;
}

STATIC_COMMAND_START
STATIC_COMMAND("lspci",
               "Enumerate the devices detected in PCIe ECAM space",
               &cmd_lspci)
STATIC_COMMAND("pciunplug",
               "Unplug the specified PCIe device (shutting down the driver if needed)",
               &cmd_pciunplug)
STATIC_COMMAND("pcireset",
               "Initiate a Function Level Reset of the specified device.",
               &cmd_pcireset)
STATIC_COMMAND("pcirescan",
               "Force a rescan of the PCIe configuration space, matching drivers to unclaimed "
               "devices as we go.  Then attempt to start all newly claimed devices.",
               &cmd_pcirescan)
STATIC_COMMAND_END(pcie);

#endif  // WITH_LIB_CONSOLE
