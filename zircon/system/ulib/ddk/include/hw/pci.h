// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <endian.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

/*
 * PCI configuration space offsets
 */
#define PCI_CONFIG_VENDOR_ID        0x00
#define PCI_CONFIG_DEVICE_ID        0x02
#define PCI_CONFIG_COMMAND          0x04
#define PCI_CONFIG_STATUS           0x06
#define PCI_CONFIG_REVISION_ID      0x08
#define PCI_CONFIG_CLASS_CODE       0x09
#define PCI_CONFIG_CLASS_CODE_INTR  0x09
#define PCI_CONFIG_CLASS_CODE_SUB   0x0a
#define PCI_CONFIG_CLASS_CODE_BASE  0x0b
#define PCI_CONFIG_CACHE_LINE_SIZE  0x0c
#define PCI_CONFIG_LATENCY_TIMER    0x0d
#define PCI_CONFIG_HEADER_TYPE      0x0e
#define PCI_CONFIG_BIST             0x0f
#define PCI_CONFIG_BASE_ADDRESSES   0x10
#define PCI_CONFIG_CARDBUS_CIS_PTR  0x28
#define PCI_CONFIG_SUBSYS_VENDOR_ID 0x2c
#define PCI_CONFIG_SUBSYS_ID        0x2e
#define PCI_CONFIG_EXP_ROM_ADDRESS  0x30
#define PCI_CONFIG_CAPABILITIES     0x34
#define PCI_CONFIG_INTERRUPT_LINE   0x3c
#define PCI_CONFIG_INTERRUPT_PIN    0x3d
#define PCI_CONFIG_MIN_GRANT        0x3e
#define PCI_CONFIG_MAX_LATENCY      0x3f

/*
 * PCI header type register bits
 */
#define PCI_HEADER_TYPE_MASK        0x7f
#define PCI_HEADER_TYPE_MULTI_FN    0x80

/*
 * PCI header types
 */
#define PCI_HEADER_TYPE_STANDARD    0x00
#define PCI_HEADER_TYPE_PCI_BRIDGE  0x01
#define PCI_HEADER_TYPE_CARD_BUS    0x02

/*
 * PCI command register bits
 */
#define PCI_COMMAND_IO_EN           0x0001
#define PCI_COMMAND_MEM_EN          0x0002
#define PCI_COMMAND_BUS_MASTER_EN   0x0004
#define PCI_COMMAND_SPECIAL_EN      0x0008
#define PCI_COMMAND_MEM_WR_INV_EN   0x0010
#define PCI_COMMAND_PAL_SNOOP_EN    0x0020
#define PCI_COMMAND_PERR_RESP_EN    0x0040
#define PCI_COMMAND_AD_STEP_EN      0x0080
#define PCI_COMMAND_SERR_EN         0x0100
#define PCI_COMMAND_FAST_B2B_EN     0x0200

/*
 * PCI status register bits
 */
#define PCI_STATUS_INTERRUPT        0x0008
#define PCI_STATUS_NEW_CAPS         0x0010
#define PCI_STATUS_66_MHZ           0x0020
#define PCI_STATUS_FAST_B2B         0x0080
#define PCI_STATUS_MSTR_PERR        0x0100
#define PCI_STATUS_DEVSEL_MASK      0x0600
#define PCI_STATUS_TARG_ABORT_SIG   0x0800
#define PCI_STATUS_TARG_ABORT_RCV   0x1000
#define PCI_STATUS_MSTR_ABORT_RCV   0x2000
#define PCI_STATUS_SERR_SIG         0x4000
#define PCI_STATUS_PERR             0x8000

#define PCI_MAX_BAR_COUNT           6u

#define PCI_CLASS_LEGACY_DEVICE     0x00
#define PCI_CLASS_MASS_STORAGE      0x01
#define PCI_CLASS_NETWORK           0x02
#define PCI_CLASS_DISPLAY           0x03
#define PCI_CLASS_MULTIMEDIA        0x04
#define PCI_CLASS_MEMORY            0x05
#define PCI_CLASS_BRIDGE            0x06
#define PCI_CLASS_SIMPLE_COMM       0x07
#define PCI_CLASS_BASE_PERIPH       0x08
#define PCI_CLASS_INPUT             0x09
#define PCI_CLASS_DOCK              0x0A
#define PCI_CLASS_PROCESSOR         0x0B
#define PCI_CLASS_SERIAL_BUS        0x0C
#define PCI_CLASS_WIRELESS          0x0D
#define PCI_CLASS_INTELLIGENT_IO    0x0E
#define PCI_CLASS_SATELLITE_COMM    0x0F
#define PCI_CLASS_ENCRYPTION        0x10
#define PCI_CLASS_DATA_ACQ          0x11
#define PCI_CLASS_UNDEFINED         0x99

// Subclasses by category
// Mass storage
#define PCI_SUBCLASS_SCSI           0x00
#define PCI_SUBCLASS_IDE            0x01
#define PCI_SUBCLASS_FLOPPY_DISK    0x02
#define PCI_SUBCLASS_IPI_BUS        0x03
#define PCI_SUBCLASS_RAID_BUS       0x04
#define PCI_SUBCLASS_ATA            0x05
#define PCI_SUBCLASS_SATA           0x06
#define PCI_SUBCLASS_SERIAL_SCSI    0x07
#define PCI_SUBCLASS_NVMEM          0x08
#define PCI_SUBCLASS_MASS_STORAGE   0x80
// Network
#define PCI_SUBCLASS_ETHERNET       0x00
#define PCI_SUBCLASS_TOKEN_RING     0x01
#define PCI_SUBCLASS_FDDI           0x02
#define PCI_SUBCLASS_ATM            0x03
#define PCI_SUBCLASS_ISDN           0x04
#define PCI_SUBCLASS_WORLDFIP       0x05
#define PCI_SUBCLASS_PICMG          0x06
#define PCI_SUBCLASS_INFINIBAND     0x07
#define PCI_SUBCLASS_FABRIC         0x08
#define PCI_SUBCLASS_NETWORK        0x80
// Display
#define PCI_SUBCLASS_VGA            0x00
#define PCI_SUBCLASS_XGA            0x01
#define PCI_SUBCLASS_3D             0x02
#define PCI_SUBCLASS_DISPLAY        0x80
// Multimedia
#define PCI_SUBCLASS_VIDEO_CTRL     0x00
#define PCI_SUBCLASS_AUDIO_CTRL     0x01
#define PCI_SUBCLASS_TELEPHONY      0x02
#define PCI_SUBCLASS_AUDIO_DEVICE   0x03
#define PCI_SUBCLASS_MULTIMEDIA     0x80
// Memory
#define PCI_SUBCLASS_RAM            0x00
#define PCI_SUBCLASS_FLASH          0x01
#define PCI_SUBCLASS_MEMORY         0x80
// Bridge
#define PCI_SUBCLASS_HOST           0x00
#define PCI_SUBCLASS_ISA            0x01
#define PCI_SUBCLASS_EISA           0x02
#define PCI_SUBCLASS_MICROCHANNEL   0x03
#define PCI_SUBCLASS_PCI            0x04
#define PCI_SUBCLASS_PCMCIA         0x05
#define PCI_SUBCLASS_NUBUS          0x06
#define PCI_SUBCLASS_CARDBUS        0x07
#define PCI_SUBCLASS_RACEWAY        0x08
#define PCI_SUBCLASS_PCI_TO_PCI     0x09
#define PCI_SUBCLASS_INFI_PCI_HOST  0x0A
#define PCI_SUBCLASS_BRIDGE         0x80
// Communication
#define PCI_SUBCLASS_SERIAL         0x00
#define PCI_SUBCLASS_PARALLEL       0x01
#define PCI_SUBCLASS_MULTI_SERIAL   0x02
#define PCI_SUBCLASS_MODEM          0x03
#define PCI_SUBCLASS_GPIB_CTRL      0x04
#define PCI_SUBCLASS_SMARDT_CARD    0x05
#define PCI_SUBCLASS_COMMUNICATION  0x80
// Generic
#define PCI_SUBCLASS_PIC            0x00
#define PCI_SUBCLASS_DMA            0x01
#define PCI_SUBCLASS_TIMER          0x02
#define PCI_SUBCLASS_RTC            0x03
#define PCI_SUBCLASS_PCI_HOTPLUG    0x04
#define PCI_SUBCLASS_SD_HOST        0x05
#define PCI_SUBCLASS_IOMMU          0x06
#define PCI_SUBCLASS_SYSTEM_PERIPH  0x80

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t revision_id_0;
    uint8_t program_interface;
    uint8_t sub_class;
    uint8_t base_class;
    uint8_t cache_line_size;
    uint8_t latency_timer;
    uint8_t header_type;
    uint8_t bist;
    uint32_t base_addresses[6];
    uint32_t cardbus_cis_ptr;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint32_t expansion_rom_address;
    uint8_t capabilities_ptr;
    uint8_t reserved_0[3];
    uint32_t reserved_1;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint8_t min_grant;
    uint8_t max_latency;
} __PACKED pci_config_t;

/*
 * Endian independent PCIe register access helpers.
 */
static inline uint8_t  pcie_read8 (const volatile uint8_t*  reg) { return *reg; }
static inline uint16_t pcie_read16(const volatile uint16_t* reg) { return le16toh(*reg); }
static inline uint32_t pcie_read32(const volatile uint32_t* reg) { return le32toh(*reg); }

static inline void pcie_write8 (volatile uint8_t*  reg, uint8_t  val) { *reg = val; }
static inline void pcie_write16(volatile uint16_t* reg, uint16_t val) { *reg = htole16(val); }
static inline void pcie_write32(volatile uint32_t* reg, uint32_t val) { *reg = htole32(val); }

__END_CDECLS;
