// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifdef WITH_LIB_CONSOLE

#include <ctype.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <lib/console.h>
#include <string.h>

#include <dev/pcie_bridge.h>
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_device.h>

static constexpr unsigned int kPciDumpRowLen = 0x10u;

class PcieDebugConsole {
public:
    static int CmdLsPci(int argc, const cmd_args *argv, uint32_t flags);
    static int CmdPciUnplug(int argc, const cmd_args *argv, uint32_t flags);
    static int CmdPciReset(int argc, const cmd_args *argv, uint32_t flags);
    static int CmdPciRescan(int argc, const cmd_args *argv, uint32_t flags);
};

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

static const char* pci_device_type(const PcieDevice& dev)
{
    // TODO(johngro): It might be a good idea, some day, to make this something
    // better than an O(n) search.

    // If this is a PCIe style bridge with a specific device type spelled out in
    // its PCI Express Capabilities structure, use that to provide the type
    // string.
    switch (dev.pcie_device_type()) {
        case PCIE_DEVTYPE_RC_ROOT_PORT:           return "PCIe Root Port";
        case PCIE_DEVTYPE_SWITCH_UPSTREAM_PORT:   return "PCIe Upstream Switch Port";
        case PCIE_DEVTYPE_SWITCH_DOWNSTREAM_PORT: return "PCIe Downstream Switch Port";
        case PCIE_DEVTYPE_PCIE_TO_PCI_BRIDGE:     return "PCIe-to-PCI Bridge";
        case PCIE_DEVTYPE_PCI_TO_PCIE_BRIDGE:     return "PCI-to-PCIe Bridge";
        default: break;
    }

    for (size_t i = 0; i < countof(PCI_DEV_TYPE_LUT); ++i) {
        const pci_dev_type_lut_entry_t* entry = PCI_DEV_TYPE_LUT + i;

        if ((dev.class_id() == entry->class_code)    &&
            (dev.subclass() == entry->subclass)      &&
            (dev.prog_if()  >= entry->prof_if_start) &&
            (dev.prog_if()  <= entry->prof_if_end))
            return entry->desc;
    }

    return pci_class_code_to_string(dev.class_id());
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

/*
 * PCI address spaces are not necessarily mapped in a manner such that
 * the address hexdump8 uses is useful, so implement one that supports
 * PIO and MMIO.
 */
static void pci_cfg_hexdump8(const PciConfig* cfg, uint16_t off, uint amt)
{
    uint8_t buf[kPciDumpRowLen];
    for (uint buf_off = off; buf_off < amt; buf_off += kPciDumpRowLen) {
        uint len = MIN(amt - buf_off, kPciDumpRowLen);

        printf("%#" PRIxPTR ": ", cfg->base() + buf_off);
        for (uint i = 0; i < len; i++)
            buf[i] = cfg->Read(PciReg8(static_cast<uint16_t>(buf_off + i)));

        for (uint i = 0; i < kPciDumpRowLen; i++ ) {
            if (i < len) {
                printf("%02x ", buf[i]);
            } else {
                printf("   ");
            }
        }

        printf("|");

        for (uint i = 0; i < len; i++) {
            if (i < len) {
                if (isgraph(buf[i]))
                    printf("%c", buf[i]);
                else
                    printf(".");
            } else {
                printf(" ");
            }
        }

        printf("\n");
    }
}

static void dump_pcie_hdr(const PcieDevice& dev, lspci_params_t* params)
{
    DEBUG_ASSERT(params);
    LSPCI_PRINTF("[%02x:%02x.%01x] - VID 0x%04x DID 0x%04x :: %s",
                 dev.bus_id(), dev.dev_id(), dev.func_id(),
                 dev.vendor_id(), dev.device_id(),
                 pci_device_type(dev));

    if (dev.disabled())
        printf(" [DISABLED]");

    if (dev.claimed())
        printf(" [CLAIMED]");

    printf("\n");
}

static void dump_pcie_bars(const PcieDevice& dev,
                           lspci_params_t* params)
{
    auto cfg = dev.config();

    DEBUG_ASSERT(dev.bar_count() <= PCIE_MAX_BAR_REGS);
    for (uint i = 0; i < dev.bar_count(); ++i) {
        LSPCI_PRINTF("Base Addr[%u]      : 0x%08x", i, cfg->Read(PciConfig::kBAR(i)));

        const pcie_bar_info_t* info = dev.GetBarInfo(i);
        if (info == nullptr) {
            printf("\n");
            continue;
        }

        printf(" :: paddr %#" PRIx64 " size %#" PRIx64 "%s%s %s%s\n",
                info->bus_addr,
                info->size,
                info->is_prefetchable ? " prefetchable" : "",
                info->is_mmio ? (info->is_64bit ? " 64-bit" : " 32-bit") : "",
                info->is_mmio ? "MMIO" : "PIO",
                info->allocation == nullptr ? "" : " (allocated)");
        if (info->vmo) {
            LSPCI_PRINTF("                               :: ");
            info->vmo->Dump(0, false);
        }
    }
}

static void dump_pcie_common(const PcieDevice& dev, lspci_params_t* params)
{
    auto cfg = dev.config();
    uint8_t base_class = cfg->Read(PciConfig::kBaseClass);

    LSPCI_PRINTF("Command           : 0x%04x\n",    cfg->Read(PciConfig::kCommand));
    LSPCI_PRINTF("Status            : 0x%04x\n",    cfg->Read(PciConfig::kStatus));
    LSPCI_PRINTF("Rev ID            : 0x%02x\n",    cfg->Read(PciConfig::kRevisionId));
    LSPCI_PRINTF("Prog Iface        : 0x%02x\n",    cfg->Read(PciConfig::kProgramInterface));
    LSPCI_PRINTF("Sub Class         : 0x%02x\n",    cfg->Read(PciConfig::kSubClass));
    LSPCI_PRINTF("Base Class        : 0x%02x %s\n", base_class,
                                                    pci_class_code_to_string(base_class));
    LSPCI_PRINTF("Cache Line Sz     : 0x%02x\n",    cfg->Read(PciConfig::kCacheLineSize));
    LSPCI_PRINTF("Latency Timer     : 0x%02x\n",    cfg->Read(PciConfig::kLatencyTimer));
    LSPCI_PRINTF("Header Type       : 0x%02x\n",    cfg->Read(PciConfig::kHeaderType));
    LSPCI_PRINTF("BIST              : 0x%02x\n",    cfg->Read(PciConfig::kBist));
}

static void dump_pcie_standard(const PcieDevice& dev, lspci_params_t* params)
{
    auto cfg = dev.config();
    LSPCI_PRINTF("Cardbus CIS       : 0x%08x\n", cfg->Read(PciConfig::kCardbusCisPtr));
    LSPCI_PRINTF("Subsystem VID     : 0x%04x\n", cfg->Read(PciConfig::kSubsystemVendorId));
    LSPCI_PRINTF("Subsystem ID      : 0x%04x\n", cfg->Read(PciConfig::kSubsystemId));
    LSPCI_PRINTF("Exp ROM addr      : 0x%08x\n", cfg->Read(PciConfig::kExpansionRomAddress));
    LSPCI_PRINTF("Cap Ptr           : 0x%02x\n", cfg->Read(PciConfig::kCapabilitiesPtr));
    LSPCI_PRINTF("IRQ line          : 0x%02x\n", cfg->Read(PciConfig::kInterruptLine));
    LSPCI_PRINTF("IRQ pin           : 0x%02x\n", cfg->Read(PciConfig::kInterruptPin));
    LSPCI_PRINTF("Min Grant         : 0x%02x\n", cfg->Read(PciConfig::kMinGrant));
    LSPCI_PRINTF("Max Latency       : 0x%02x\n", cfg->Read(PciConfig::kMaxLatency));
}

static void dump_pcie_bridge(const PcieBridge& bridge, lspci_params_t* params)
{
    auto cfg = bridge.config();

    LSPCI_PRINTF("P. Bus ID         : 0x%02x\n", cfg->Read(PciConfig::kPrimaryBusId));
    LSPCI_PRINTF("S. Bus Range      : [0x%02x, 0x%02x]\n",
                                                 cfg->Read(PciConfig::kSecondaryBusId),
                                                 cfg->Read(PciConfig::kSubordinateBusId));
    LSPCI_PRINTF("S. Latency Timer  : 0x%02x\n", cfg->Read(PciConfig::kSecondaryLatencyTimer));
    LSPCI_PRINTF("IO Base           : 0x%02x\n", cfg->Read(PciConfig::kIoBase));
    LSPCI_PRINTF("IO Base Upper     : 0x%04x\n", cfg->Read(PciConfig::kIoBaseUpper));
    LSPCI_PRINTF("IO Limit          : 0x%02x\n", cfg->Read(PciConfig::kIoLimit));
    LSPCI_PRINTF("IO Limit Upper    : 0x%04x",   cfg->Read(PciConfig::kIoLimitUpper));
    if (bridge.io_base() < bridge.io_limit()) {
        printf(" :: [0x%08x, 0x%08x]\n", bridge.io_base(), bridge.io_limit());
    } else {
        printf("\n");
    }
    LSPCI_PRINTF("Secondary Status  : 0x%04x\n", cfg->Read(PciConfig::kSecondaryStatus));
    LSPCI_PRINTF("Memory Limit      : 0x%04x\n", cfg->Read(PciConfig::kMemoryLimit));
    LSPCI_PRINTF("Memory Base       : 0x%04x", cfg->Read(PciConfig::kMemoryBase));
    if (bridge.mem_base() < bridge.mem_limit()) {
        printf(" :: [0x%08x, 0x%08x]\n", bridge.mem_base(), bridge.mem_limit());
    } else {
        printf("\n");
    }
    LSPCI_PRINTF("PFMem Base        : 0x%04x\n", cfg->Read(PciConfig::kPrefetchableMemoryBase));
    LSPCI_PRINTF("PFMem Base Upper  : 0x%08x\n", cfg->Read(PciConfig::kPrefetchableMemoryBaseUpper));
    LSPCI_PRINTF("PFMem Limit       : 0x%04x\n", cfg->Read(PciConfig::kPrefetchableMemoryLimit));
    LSPCI_PRINTF("PFMem Limit Upper : 0x%08x", cfg->Read(PciConfig::kPrefetchableMemoryLimitUpper));
    if (bridge.pf_mem_base() < bridge.pf_mem_limit()) {
        printf(" :: [0x%016" PRIx64 ", 0x%016" PRIx64"]\n",
                bridge.pf_mem_base(), bridge.pf_mem_limit());
    } else {
        printf("\n");
    }

    LSPCI_PRINTF("Capabilities Ptr  : 0x%02x\n", cfg->Read(PciConfig::kCapabilitiesPtr));
    LSPCI_PRINTF("Exp ROM Address   : 0x%08x\n", cfg->Read(PciConfig::kExpansionRomAddress));
    LSPCI_PRINTF("Interrupt Line    : 0x%02x\n", cfg->Read(PciConfig::kInterruptLine));
    LSPCI_PRINTF("Interrupt Pin     : 0x%02x\n", cfg->Read(PciConfig::kInterruptPin));
    LSPCI_PRINTF("Bridge Control    : 0x%04x\n", cfg->Read(PciConfig::kBridgeControl));
}

static void dump_pcie_raw_config(uint amt, const PciConfig* cfg)
{
    DEBUG_ASSERT(amt == PCIE_BASE_CONFIG_SIZE || amt == PCIE_EXTENDED_CONFIG_SIZE);
    printf("%u bytes of raw config (base %s:%#" PRIxPTR ")\n",
           amt, (cfg->addr_space() == PciAddrSpace::MMIO) ? "MMIO" : "PIO", cfg->base());

    pci_cfg_hexdump8(cfg, 0, amt);
}

#define CAP_TBL_ENTRY(s) (s, #s)
static struct _cap_tbl {
    uint8_t id;
    const char *label;
} cap_tbl[] = {
    { PCIE_CAP_ID_PCI_PWR_MGMT, "PCI_PWR_MGMT" },
    { PCIE_CAP_ID_AGP, "AGP" },
    { PCIE_CAP_ID_VPD, "VPD" },
    { PCIE_CAP_ID_MSI, "MSI" },
    { PCIE_CAP_ID_PCIX, "PCIX" },
    { PCIE_CAP_ID_HYPERTRANSPORT, "HYPERTRANSPORT" },
    { PCIE_CAP_ID_VENDOR, "VENDOR" },
    { PCIE_CAP_ID_DEBUG_PORT, "DEBUG_PORT" },
    { PCIE_CAP_ID_COMPACTPCI_CRC, "COMPACTPCI_CRC" },
    { PCIE_CAP_ID_PCI_HOTPLUG, "PCI_HOTPLUG" },
    { PCIE_CAP_ID_PCI_BRIDGE_SUBSYSTEM_VID, "PCI_BRIDGE_SUBSYSTEM_VID" },
    { PCIE_CAP_ID_AGP_8X, "AGP_8X" },
    { PCIE_CAP_ID_SECURE_DEVICE, "SECURE_DEVICE" },
    { PCIE_CAP_ID_PCI_EXPRESS, "PCI_EXPRESS" },
    { PCIE_CAP_ID_MSIX, "MSIX" },
    { PCIE_CAP_ID_SATA_DATA_NDX_CFG, "SATA_DATA_NDX_CFG" },
    { PCIE_CAP_ID_ADVANCED_FEATURES, "ADVANCED_FEATURES" },
    { PCIE_CAP_ID_ENHANCED_ALLOCATION, "ENHANCED_ALLOCATION" },
};
#undef CAP_TABLE_ENTRY

static inline const char* get_cap_str(uint8_t id) {
    for (const auto& cur : cap_tbl) {
        if (cur.id == id) {
            return cur.label;
        }
    }

    return "<Unknown>";
}

static void dump_pcie_capabilities(mxtl::RefPtr<PcieDevice> dev, void *ctx)
{
    bool is_first = true;
    lspci_params_t* params = static_cast<lspci_params_t*>(ctx);
    auto initial_indent = params->indent_level;
    params->indent_level += 2;

    if (!dev->capabilities().is_empty()) {
        LSPCI_PRINTF("Std Capabilities  :");
        for (const auto& cap : dev->capabilities()) {
            if (is_first) {
                printf(" %s (%#02x)\n", get_cap_str(cap.id()), cap.id());
                is_first = false;
                params->indent_level += 10;
            } else {
                LSPCI_PRINTF("%s (%#02x)\n", get_cap_str(cap.id()), cap.id());
            }
        }
    }

    params->indent_level = initial_indent;
}

static bool dump_pcie_device(const mxtl::RefPtr<PcieDevice>& dev, void* ctx, uint level)
{
    DEBUG_ASSERT(dev && ctx);
    lspci_params_t* params = (lspci_params_t*)ctx;
    bool match;
    auto cfg = dev->config();

    /* Grab the device's lock so it cannot be unplugged out from under us while
     * we print details. */
    AutoLock lock(dev->dev_lock());

    /* If the device has already been unplugged, just skip it */
    if (!dev->plugged_in())
        return true;

    match = (((params->bus_id  == WILDCARD_ID) || (params->bus_id  == dev->bus_id())) &&
             ((params->dev_id  == WILDCARD_ID) || (params->dev_id  == dev->dev_id())) &&
             ((params->func_id == WILDCARD_ID) || (params->func_id == dev->func_id())));
    if (!match)
        return true;

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

        if (dev->config_vmo()) {
            LSPCI_PRINTF("Config VMO        : ");
            dev->config_vmo()->Dump(0, false);
        }

        dump_pcie_common(*dev, params);
        dump_pcie_bars(*dev, params);

        uint8_t header_type = cfg->Read(PciConfig::kHeaderType) & PCI_HEADER_TYPE_MASK;
        switch (header_type) {
        case PCI_HEADER_TYPE_STANDARD:
            dump_pcie_standard(*dev, params);
            break;

        case PCI_HEADER_TYPE_PCI_BRIDGE: {
            if (dev->is_bridge()) {
                dump_pcie_bridge(*static_cast<PcieBridge*>(dev.get()), params);
            } else {
                printf("ERROR! Type 1 header detected for non-bridge device!\n");
            }
        } break;

        case PCI_HEADER_TYPE_CARD_BUS:
            printf("TODO : Implemnt CardBus Config header register dump\n");
            break;

        default:
            printf("Unknown Header Type (0x%02x)\n", header_type);
            break;
        }

        params->indent_level -= 2;
        dump_pcie_capabilities(dev, params);
    }

    if (params->cfg_dump_amt)
        dump_pcie_raw_config(params->cfg_dump_amt, dev->config());

    return true;
}

int PcieDebugConsole::CmdLsPci(int argc, const cmd_args *argv, uint32_t flags) {
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

    auto bus_drv = PcieBusDriver::GetDriver();
    if (bus_drv == nullptr)
        return ERR_BAD_STATE;

    bus_drv->ForeachDevice(dump_pcie_device, &params);

    if (!params.found && params.force_dump_cfg &&
        (params.bus_id  != WILDCARD_ID) &&
        (params.dev_id  != WILDCARD_ID) &&
        (params.func_id != WILDCARD_ID)) {
        const PciConfig* cfg;

        cfg = bus_drv->GetConfig(params.bus_id, params.dev_id, params.func_id);
        if (!cfg) {
            printf("Config space for %02x:%02x.%01x not mapped by bus driver!\n",
                   params.bus_id, params.dev_id, params.func_id);
        } else {
            dump_pcie_raw_config(params.cfg_dump_amt, cfg);
        }
    } else {
        printf("PCIe scan discovered %u device%s\n", params.found, (params.found == 1) ? "" : "s");
    }

    return NO_ERROR;
}

int PcieDebugConsole::CmdPciUnplug(int argc, const cmd_args *argv, uint32_t flags) {
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

    auto bus_drv = PcieBusDriver::GetDriver();
    if (bus_drv == nullptr)
        return ERR_BAD_STATE;

    mxtl::RefPtr<PcieDevice> dev = bus_drv->GetRefedDevice(bus_id, dev_id, func_id);

    if (!dev) {
        printf("Failed to find PCI device %02x:%02x.%01x\n", bus_id, dev_id, func_id);
    } else {
        printf("Unplugging PCI device %02x:%02x.%x...\n",
                bus_id, dev_id, func_id);
        dev->Unplug();
        dev = nullptr;
        printf("done\n");
    }

    return NO_ERROR;
}

int PcieDebugConsole::CmdPciReset(int argc, const cmd_args *argv, uint32_t flags) {
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

    auto bus_drv = PcieBusDriver::GetDriver();
    if (bus_drv == nullptr)
        return ERR_BAD_STATE;

    mxtl::RefPtr<PcieDevice> dev = bus_drv->GetRefedDevice(bus_id, dev_id, func_id);

    if (!dev) {
        printf("Failed to find PCI device %02x:%02x.%01x\n", bus_id, dev_id, func_id);
    } else {
        printf("Attempting reset of device %02x:%02x.%01x...\n", bus_id, dev_id, func_id);
        status_t res = dev->DoFunctionLevelReset();
        dev = nullptr;
        if (res != NO_ERROR)
            printf("Reset attempt failed (res = %d).\n", res);
        else
            printf("Success, device %02x:%02x.%01x has been reset.\n", bus_id, dev_id, func_id);
    }

    return NO_ERROR;
}

int PcieDebugConsole::CmdPciRescan(int argc, const cmd_args *argv, uint32_t flags) {
    auto bus_drv = PcieBusDriver::GetDriver();
    if (bus_drv == nullptr)
        return ERR_BAD_STATE;

    return bus_drv->RescanDevices();
}

STATIC_COMMAND_START
STATIC_COMMAND("lspci",
               "Enumerate the devices detected in PCIe ECAM space",
               &PcieDebugConsole::CmdLsPci)
STATIC_COMMAND("pciunplug",
               "Force \"unplug\" the specified PCIe device",
               &PcieDebugConsole::CmdPciUnplug)
STATIC_COMMAND("pcireset",
               "Initiate a Function Level Reset of the specified device.",
               &PcieDebugConsole::CmdPciReset)
STATIC_COMMAND("pcirescan",
               "Force a rescan of the PCIe configuration space, matching drivers to unclaimed "
               "devices as we go.  Then attempt to start all newly claimed devices.",
               &PcieDebugConsole::CmdPciRescan)
STATIC_COMMAND_END(pcie);

#endif  // WITH_LIB_CONSOLE
