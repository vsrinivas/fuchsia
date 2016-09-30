// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#pragma once

#include <assert.h>
#include <magenta/compiler.h>
#include <dev/pci.h>
#include <dev/pcie_constants.h>
#include <endian.h>
#include <err.h>
#include <sys/types.h>

__BEGIN_CDECLS

/**
 * Standard PCI/PCIe capability headers are 16 bits long, with the 8 LSB being
 * the type field, and the 8 MSB being the next pointer.  Note, the 2 LSBs of
 * the next pointer are reserved for future use and must be masked by system
 * software to ensure future compatibility.  @see Section 6.7 of the PCI Local
 * Bus specificiaion v3.0.
 */
typedef uint16_t pcie_cap_hdr_t;

static inline uint8_t pcie_cap_hdr_get_type(pcie_cap_hdr_t hdr) {
    return (uint8_t)(hdr & 0xFF);
}

static inline uint8_t pcie_cap_hdr_get_next_ptr(pcie_cap_hdr_t hdr) {
    return (uint8_t)((hdr >> 8) & 0xFC);
}

/**
 * @see PCI Code and ID Assignment Specification Revision 1.7 Section 2
 */
#define PCIE_CAP_ID_NULL                     0x00
#define PCIE_CAP_ID_PCI_PWR_MGMT             0x01
#define PCIE_CAP_ID_AGP                      0x02
#define PCIE_CAP_ID_VPD                      0x03
#define PCIE_CAP_ID_MSI                      0x05
#define PCIE_CAP_ID_PCIX                     0x07
#define PCIE_CAP_ID_HYPERTRANSPORT           0x08
#define PCIE_CAP_ID_VENDOR                   0x09
#define PCIE_CAP_ID_DEBUG_PORT               0x0A
#define PCIE_CAP_ID_COMPACTPCI_CRC           0x0B
#define PCIE_CAP_ID_PCI_HOTPLUG              0x0C
#define PCIE_CAP_ID_PCI_BRIDGE_SUBSYSTEM_VID 0x0D
#define PCIE_CAP_ID_AGP_8X                   0x0E
#define PCIE_CAP_ID_SECURE_DEVICE            0x0F
#define PCIE_CAP_ID_PCI_EXPRESS              0x10
#define PCIE_CAP_ID_MSIX                     0x11
#define PCIE_CAP_ID_SATA_DATA_NDX_CFG        0x12
#define PCIE_CAP_ID_ADVANCED_FEATURES        0x13
#define PCIE_CAP_ID_ENHANCED_ALLOCATION      0x14

/**
 * Structure definitions for capability PCIE_CAP_ID_MSI
 *
 * @see The PCI Local Bus specificiaion v3.0 Section 6.8.1
 */
typedef struct pcie_cap_msi {
    pcie_cap_hdr_t hdr;
    uint16_t       ctrl;
    uint32_t       addr;

    union {
        struct {
            uint16_t data;
        } nopvm_32bit;

        struct {
            uint16_t data;
            uint16_t __rsvd;
            uint32_t mask_bits;
            uint32_t pending_bits;
        } pvm_32bit;

        struct {
            uint32_t addr_upper;
            uint16_t data;
        } nopvm_64bit;

        struct {
            uint32_t addr_upper;
            uint16_t data;
            uint16_t __rsvd;
            uint32_t mask_bits;
            uint32_t pending_bits;
        } pvm_64bit;
    };
} __PACKED pcie_cap_msi_t;

#define PCIE_CAP_MSI_CAP_HDR_SIZE               (offsetof(pcie_cap_msi_t, nopvm_32bit))
#define PCIE_CAP_MSI_CTRL_PVM_SUPPORTED(ctrl)   ((ctrl & 0x0100) != 0)
#define PCIE_CAP_MSI_CTRL_64BIT_SUPPORTED(ctrl) ((ctrl & 0x0080) != 0)
#define PCIE_CAP_MSI_CTRL_GET_MME(ctrl)         ((ctrl >> 4) & 0x7)
#define PCIE_CAP_MSI_CTRL_GET_MMC(ctrl)         ((ctrl >> 1) & 0x7)
#define PCIE_CAP_MSI_CTRL_GET_ENB(ctrl)         ((ctrl & 0x0001) != 0)

#define PCIE_CAP_MSI_CTRL_SET_MME(val, ctrl)    (uint16_t)((ctrl & ~0x0070) | ((val & 0x7) << 4))
#define PCIE_CAP_MSI_CTRL_SET_ENB(val, ctrl)    (uint16_t)((ctrl & ~0x0001) | (!!val))

/**
 * Structure definitions for capability PCIE_CAP_ID_MSIX and the tables it
 * refers to.
 *
 * @see The PCI Local Bus specificiaion v3.0 Section 6.8.2
 */
typedef struct pcie_cap_msix {
    pcie_cap_hdr_t hdr;
    uint16_t       ctrl;
    uint32_t       vector_table_bir_offset;
    uint32_t       pba_table_bir_offset;
} __PACKED pcie_cap_msix_t;

typedef struct pcie_msix_vector_entry {
    uint32_t addr;
    uint32_t addr_upper;
    uint32_t data;
    uint32_t vector_ctrl;
} __PACKED pcie_msix_vector_entry_t;

/**
 * Structure and type definitions for capability PCIE_CAP_ID_PCI_EXPRESS
 *
 * @see The PCI Express Base Spec v3.1a, Section 7.8
 */
typedef struct pcie_caps_hdr {
    pcie_cap_hdr_t hdr;
    uint16_t       caps;
} __PACKED pcie_caps_hdr_t;

typedef struct pcie_caps_chunk {
    uint32_t caps;
    uint16_t ctrl;
    uint16_t status;
} __PACKED pcie_caps_chunk_t;

typedef struct pcie_caps_root_chunk {
    uint16_t ctrl;
    uint16_t caps;
    uint32_t status;
} __PACKED pcie_caps_root_chunk_t;

typedef struct pcie_capabilities {
    pcie_caps_hdr_t        hdr;

    pcie_caps_chunk_t      device;
    pcie_caps_chunk_t      link;
    pcie_caps_chunk_t      slot;

    pcie_caps_root_chunk_t root;

    pcie_caps_chunk_t      device2;
    pcie_caps_chunk_t      link2;
    pcie_caps_chunk_t      slot2;
} __PACKED pcie_capabilities_t;

#define PCS_CAPS_V1_ENDPOINT_SIZE        ((uint)offsetof(pcie_capabilities_t, link))
#define PCS_CAPS_V1_UPSTREAM_PORT_SIZE   ((uint)offsetof(pcie_capabilities_t, slot))
#define PCS_CAPS_V1_DOWNSTREAM_PORT_SIZE ((uint)offsetof(pcie_capabilities_t, root))
#define PCS_CAPS_V1_ROOT_PORT_SIZE       ((uint)offsetof(pcie_capabilities_t, device2))
#define PCS_CAPS_V2_SIZE                 ((uint)sizeof(pcie_capabilities_t))
#define PCS_CAPS_MIN_SIZE                ((uint)offsetof(pcie_capabilities_t, device))

#define PCS_CAPS_DEV_CHUNK_NDX    (0u)
#define PCS_CAPS_LINK_CHUNK_NDX   (1u)
#define PCS_CAPS_SLOT_CHUNK_NDX   (2u)
#define PCS_CAPS_DEV2_CHUNK_NDX   (3u)
#define PCS_CAPS_LINK2_CHUNK_NDX  (4u)
#define PCS_CAPS_SLOT2_CHUNK_NDX  (5u)
#define PCS_CAPS_CHUNK_COUNT      (6u)

enum pcie_device_type_t : uint8_t {
    // Type 0 config header types
    PCIE_DEVTYPE_PCIE_ENDPOINT          = 0x0,
    PCIE_DEVTYPE_LEGACY_PCIE_ENDPOINT   = 0x1,
    PCIE_DEVTYPE_RC_INTEGRATED_ENDPOINT = 0x9,
    PCIE_DEVTYPE_RC_EVENT_COLLECTOR     = 0xA,

    // Type 1 config header types
    PCIE_DEVTYPE_RC_ROOT_PORT           = 0x4,
    PCIE_DEVTYPE_SWITCH_UPSTREAM_PORT   = 0x5,
    PCIE_DEVTYPE_SWITCH_DOWNSTREAM_PORT = 0x6,
    PCIE_DEVTYPE_PCIE_TO_PCI_BRIDGE     = 0x7,
    PCIE_DEVTYPE_PCI_TO_PCIE_BRIDGE     = 0x8,

    // Default value; used for device which have no pcie_capabilities extension.
    PCIE_DEVTYPE_UNKNOWN = 0xFF,
};

// Section 7.8.2 Table 7-12
#define PCS_CAPS_VERSION(val)     (((val) >> 0) &  0xF)
#define PCS_CAPS_DEVTYPE(val)     ((pcie_device_type_t)(((val) >> 4) &  0xF))
#define PCS_CAPS_SLOT_IMPL(val)   (((val) >> 8) &  0x1)
#define PCS_CAPS_IRQ_MSG_NUM(val) (((val) >> 9) & 0x1F)

// Section 7.8.3 Table 7-13
#define PCS_DEV_CAPS_MAX_PAYLOAD_SIZE(val)         (((val) >>  0) & 0x07)
#define PCS_DEV_CAPS_PHANTOM_FUNC_SUPPORTED(val)   (((val) >>  3) & 0x03)
#define PCS_DEV_CAPS_EXT_TAG_SUPPORTED(val)        (((val) >>  5) & 0x01)
#define PCS_DEV_CAPS_MAX_EL0_LATENCY(val)          (((val) >>  6) & 0x07)
#define PCS_DEV_CAPS_MAX_EL1_LATENCY(val)          (((val) >>  9) & 0x07)
#define PCS_DEV_CAPS_ROLE_BASED_ERR_REP(val)       (((val) >> 15) & 0x01)
#define PCS_DEV_CAPS_CAP_SLOT_PWR_LIMIT_VAL(val)   (((val) >> 18) & 0xFF)
#define PCS_DEV_CAPS_CAP_SLOT_PWR_LIMIT_SCALE(val) (((val) >> 26) & 0x03)
#define PCS_DEV_CAPS_FUNC_LEVEL_RESET(val)         (((val) >> 28) & 0x01)

/**
 * Structure and type definitions for capability PCIE_CAP_ID_ADVANCED_FEATURES
 *
 * @see The Advanced Capabilities for Conventional PCI ECN
 */
typedef struct pcie_cap_adv_caps {
    pcie_cap_hdr_t    hdr;
    uint8_t           length;
    uint8_t           af_caps;
    uint8_t           af_ctrl;
    uint8_t           af_status;
} pcie_cap_adv_caps_t;

#define PCS_ADVCAPS_CAP_HAS_FUNC_LEVEL_RESET(val)   ((((val) >> 1) & 0x01) != 0)
#define PCS_ADVCAPS_CAP_HAS_TRANS_PENDING(val)      ((((val) >> 0) & 0x01) != 0)
#define PCS_ADVCAPS_CTRL_INITIATE_FLR               (0x01)
#define PCS_ADVCAPS_STATUS_TRANS_PENDING            (0x01)

// TODO(johngro) : so many other bitfields to define... eventually, get around
// to doing so.

/**
 * Extended PCIe capability headers are 32 bits long with the following packing.
 *
 * [0:15]  : 16-bit Extended Capability ID.
 * [16:19] : 4-bit Capability Version.
 * [20:31] : Next pointer; 2 LSB must be masked by system software.
 */
typedef uint32_t pcie_ext_cap_hdr_t;

static inline uint16_t pcie_ext_cap_hdr_get_type(pcie_ext_cap_hdr_t hdr) {
    return (uint16_t)(hdr & 0xFFFF);
}

static inline uint8_t pcie_ext_cap_hdr_get_cap_version(pcie_ext_cap_hdr_t hdr) {
    return (uint8_t)((hdr >> 16) & 0xF);
}

static inline uint16_t pcie_ext_cap_hdr_get_next_ptr(pcie_ext_cap_hdr_t hdr) {
    return (uint16_t)((hdr >> 20) & 0xFFC);
}

/**
 * @see PCI Code and ID Assignment Specification Revision 1.7 Section 3
 */
#define PCIE_EXT_CAP_ID_NULL                                    0x0000
#define PCIE_EXT_CAP_ID_ADVANCED_ERROR_REPORTING                0x0001
#define PCIE_EXT_CAP_ID_VIRTUAL_CHANNEL_NO_MFVC                 0x0002
#define PCIE_EXT_CAP_ID_DEVICE_SERIAL_NUMBER                    0x0003
#define PCIE_EXT_CAP_ID_POWER_BUDGETING                         0x0004
#define PCIE_EXT_CAP_ID_ROOT_COMPLEX_LINK_DECLARATION           0x0005
#define PCIE_EXT_CAP_ID_ROOT_COMPLEX_INTERNAL_LINK_CONTROL      0x0006
#define PCIE_EXT_CAP_ID_ROOT_COMPLEX_EVENT_COLLECTOR_EP_ASSOC   0x0007
#define PCIE_EXT_CAP_ID_MULTI_FUNCTION_VIRTUAL_CHANNEL          0x0008
#define PCIE_EXT_CAP_ID_VIRTUAL_CHANNEL_MFVC                    0x0009
#define PCIE_EXT_CAP_ID_ROOT_COMPLEX_REGISTER_BLOCK             0x000A
#define PCIE_EXT_CAP_ID_VENDOR_SPECIFIC                         0x000B
#define PCIE_EXT_CAP_ID_CONFIGURATION_ACCESS_CORRELATION        0x000C
#define PCIE_EXT_CAP_ID_ACCESS_CONTROL_SERVICES                 0x000D
#define PCIE_EXT_CAP_ID_ALTERNATIVE_ROUTING_ID_INTERPRETATION   0x000E
#define PCIE_EXT_CAP_ID_ADDRESS_TRANSLATION_SERVICES            0x000F
#define PCIE_EXT_CAP_ID_SINGLE_ROOT_IO_VIRTUALIZATION           0x0010
#define PCIE_EXT_CAP_ID_MULTI_ROOT_IO_VIRTUALIZATION            0x0011
#define PCIE_EXT_CAP_ID_MULTICAST                               0x0012
#define PCIE_EXT_CAP_ID_PAGE_REQUEST                            0x0013
#define PCIE_EXT_CAP_ID_RESERVED_FOR_AMD                        0x0014
#define PCIE_EXT_CAP_ID_RESIZABLE_BAR                           0x0015
#define PCIE_EXT_CAP_ID_DYNAMIC_POWER_ALLOCATION                0x0016
#define PCIE_EXT_CAP_ID_TLP_PROCESSING_HINTS                    0x0017
#define PCIE_EXT_CAP_ID_LATENCY_TOLERANCE_REPORTING             0x0018
#define PCIE_EXT_CAP_ID_SECONDARY_PCI_EXPRESS                   0x0019
#define PCIE_EXT_CAP_ID_PROTOCOL_MULTIPLEXING                   0x001A
#define PCIE_EXT_CAP_ID_PROCESS_ADDRESS_SPACE_ID                0x001B
#define PCIE_EXT_CAP_ID_LN_REQUESTER                            0x001C
#define PCIE_EXT_CAP_ID_DOWNSTREAM_PORT_CONTAINMENT             0x001D
#define PCIE_EXT_CAP_ID_L1_PM_SUBSTATES                         0x001E
#define PCIE_EXT_CAP_ID_PRECISION_TIME_MEASUREMENT              0x001F
#define PCIE_EXT_CAP_ID_PCI_EXPRESS_OVER_MPHY                   0x0020
#define PCIE_EXT_CAP_ID_FRS_QUEUEING                            0x0021
#define PCIE_EXT_CAP_ID_READINESS_TIME_REPORTING                0x0022
#define PCIE_EXT_CAP_ID_DESIGNATED_VENDOR_SPECIFIC              0x0023

__END_CDECLS

