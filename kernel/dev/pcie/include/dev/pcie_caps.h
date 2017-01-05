// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#pragma once

#include <assert.h>
#include <magenta/compiler.h>
#include <dev/pci_config.h>
#include <dev/pci_common.h>
#include <endian.h>
#include <err.h>
#include <mxtl/intrusive_single_list.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/ref_counted.h>
#include <sys/types.h>

/**
 * @see PCI Code and ID Assignment Specification Revision 1.7 Section 2
 * @see PCI Local Bus Spec v3.0 Appendix H: Capability IDs
 */
constexpr uint8_t PCIE_CAP_ID_NULL                     = 0x00;
constexpr uint8_t PCIE_CAP_ID_PCI_PWR_MGMT             = 0x01;
constexpr uint8_t PCIE_CAP_ID_AGP                      = 0x02;
constexpr uint8_t PCIE_CAP_ID_VPD                      = 0x03;
constexpr uint8_t PCIE_CAP_ID_MSI                      = 0x05;
constexpr uint8_t PCIE_CAP_ID_PCIX                     = 0x07;
constexpr uint8_t PCIE_CAP_ID_HYPERTRANSPORT           = 0x08;
constexpr uint8_t PCIE_CAP_ID_VENDOR                   = 0x09;
constexpr uint8_t PCIE_CAP_ID_DEBUG_PORT               = 0x0A;
constexpr uint8_t PCIE_CAP_ID_COMPACTPCI_CRC           = 0x0B;
constexpr uint8_t PCIE_CAP_ID_PCI_HOTPLUG              = 0x0C;
constexpr uint8_t PCIE_CAP_ID_PCI_BRIDGE_SUBSYSTEM_VID = 0x0D;
constexpr uint8_t PCIE_CAP_ID_AGP_8X                   = 0x0E;
constexpr uint8_t PCIE_CAP_ID_SECURE_DEVICE            = 0x0F;
constexpr uint8_t PCIE_CAP_ID_PCI_EXPRESS              = 0x10;
constexpr uint8_t PCIE_CAP_ID_MSIX                     = 0x11;
constexpr uint8_t PCIE_CAP_ID_SATA_DATA_NDX_CFG        = 0x12;
constexpr uint8_t PCIE_CAP_ID_ADVANCED_FEATURES        = 0x13;
constexpr uint8_t PCIE_CAP_ID_ENHANCED_ALLOCATION      = 0x14;

/**
 * Structure definitions for capability PCIE_CAP_ID_MSI
 *
 * @see The PCI Local Bus specificiaion v3.0 Section 6.8.1
 */
#define PCIE_CAP_MSI_CAP_HDR_SIZE               (offsetof(pcie_cap_msi_t, nopvm_32bit))
#define PCIE_CAP_MSI_CTRL_PVM_SUPPORTED(ctrl)   ((ctrl & 0x0100) != 0)
#define PCIE_CAP_MSI_CTRL_64BIT_SUPPORTED(ctrl) ((ctrl & 0x0080) != 0)
#define PCIE_CAP_MSI_CTRL_GET_MME(ctrl)         ((ctrl >> 4) & 0x7)
#define PCIE_CAP_MSI_CTRL_GET_MMC(ctrl)         ((ctrl >> 1) & 0x7)
#define PCIE_CAP_MSI_CTRL_GET_ENB(ctrl)         ((ctrl & 0x0001) != 0)

#define PCIE_CAP_MSI_CTRL_SET_MME(val, ctrl)    (uint16_t)((ctrl & ~0x0070) | ((val & 0x7) << 4))
#define PCIE_CAP_MSI_CTRL_SET_ENB(val, ctrl)    (uint16_t)((ctrl & ~0x0001) | (!!val))
#define PCS_CAPS_V1_ENDPOINT_SIZE        ((uint)offsetof(pcie_capabilities_t, link))
#define PCS_CAPS_V1_UPSTREAM_PORT_SIZE   ((uint)offsetof(pcie_capabilities_t, slot))
#define PCS_CAPS_V1_DOWNSTREAM_PORT_SIZE ((uint)offsetof(pcie_capabilities_t, root))
#define PCS_CAPS_V1_ROOT_PORT_SIZE       ((uint)offsetof(pcie_capabilities_t, device2))
#define PCS_CAPS_V2_SIZE                 ((uint)sizeof(pcie_capabilities_t))
#define PCS_CAPS_MIN_SIZE                ((uint)offsetof(pcie_capabilities_t, device))

constexpr uint8_t PCS_CAPS_DEV_CHUNK_NDX    = 0;
constexpr uint8_t PCS_CAPS_LINK_CHUNK_NDX   = 1;
constexpr uint8_t PCS_CAPS_SLOT_CHUNK_NDX   = 2;
constexpr uint8_t PCS_CAPS_DEV2_CHUNK_NDX   = 3;
constexpr uint8_t PCS_CAPS_LINK2_CHUNK_NDX  = 4;
constexpr uint8_t PCS_CAPS_SLOT2_CHUNK_NDX  = 5;
constexpr uint8_t PCS_CAPS_CHUNK_COUNT      = 6;

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

#define PCS_ADVCAPS_CAP_HAS_FUNC_LEVEL_RESET(val)   ((((val) >> 1) & 0x01) != 0)
#define PCS_ADVCAPS_CAP_HAS_TRANS_PENDING(val)      ((((val) >> 0) & 0x01) != 0)
#define PCS_ADVCAPS_CTRL_INITIATE_FLR               (0x01)
#define PCS_ADVCAPS_STATUS_TRANS_PENDING            (0x01)
#define PCS_ADVCAPS_LENGTH                          (6u)

/**
 * @see PCI Code and ID Assignment Specification Revision 1.7 Section 3
 */
constexpr uint16_t PCIE_EXT_CAP_ID_NULL                                    = 0x0000;
constexpr uint16_t PCIE_EXT_CAP_ID_ADVANCED_ERROR_REPORTING                = 0x0001;
constexpr uint16_t PCIE_EXT_CAP_ID_VIRTUAL_CHANNEL_NO_MFVC                 = 0x0002;
constexpr uint16_t PCIE_EXT_CAP_ID_DEVICE_SERIAL_NUMBER                    = 0x0003;
constexpr uint16_t PCIE_EXT_CAP_ID_POWER_BUDGETING                         = 0x0004;
constexpr uint16_t PCIE_EXT_CAP_ID_ROOT_COMPLEX_LINK_DECLARATION           = 0x0005;
constexpr uint16_t PCIE_EXT_CAP_ID_ROOT_COMPLEX_INTERNAL_LINK_CONTROL      = 0x0006;
constexpr uint16_t PCIE_EXT_CAP_ID_ROOT_COMPLEX_EVENT_COLLECTOR_EP_ASSOC   = 0x0007;
constexpr uint16_t PCIE_EXT_CAP_ID_MULTI_FUNCTION_VIRTUAL_CHANNEL          = 0x0008;
constexpr uint16_t PCIE_EXT_CAP_ID_VIRTUAL_CHANNEL_MFVC                    = 0x0009;
constexpr uint16_t PCIE_EXT_CAP_ID_ROOT_COMPLEX_REGISTER_BLOCK             = 0x000A;
constexpr uint16_t PCIE_EXT_CAP_ID_VENDOR_SPECIFIC                         = 0x000B;
constexpr uint16_t PCIE_EXT_CAP_ID_CONFIGURATION_ACCESS_CORRELATION        = 0x000C;
constexpr uint16_t PCIE_EXT_CAP_ID_ACCESS_CONTROL_SERVICES                 = 0x000D;
constexpr uint16_t PCIE_EXT_CAP_ID_ALTERNATIVE_ROUTING_ID_INTERPRETATION   = 0x000E;
constexpr uint16_t PCIE_EXT_CAP_ID_ADDRESS_TRANSLATION_SERVICES            = 0x000F;
constexpr uint16_t PCIE_EXT_CAP_ID_SINGLE_ROOT_IO_VIRTUALIZATION           = 0x0010;
constexpr uint16_t PCIE_EXT_CAP_ID_MULTI_ROOT_IO_VIRTUALIZATION            = 0x0011;
constexpr uint16_t PCIE_EXT_CAP_ID_MULTICAST                               = 0x0012;
constexpr uint16_t PCIE_EXT_CAP_ID_PAGE_REQUEST                            = 0x0013;
constexpr uint16_t PCIE_EXT_CAP_ID_RESERVED_FOR_AMD                        = 0x0014;
constexpr uint16_t PCIE_EXT_CAP_ID_RESIZABLE_BAR                           = 0x0015;
constexpr uint16_t PCIE_EXT_CAP_ID_DYNAMIC_POWER_ALLOCATION                = 0x0016;
constexpr uint16_t PCIE_EXT_CAP_ID_TLP_PROCESSING_HINTS                    = 0x0017;
constexpr uint16_t PCIE_EXT_CAP_ID_LATENCY_TOLERANCE_REPORTING             = 0x0018;
constexpr uint16_t PCIE_EXT_CAP_ID_SECONDARY_PCI_EXPRESS                   = 0x0019;
constexpr uint16_t PCIE_EXT_CAP_ID_PROTOCOL_MULTIPLEXING                   = 0x001A;
constexpr uint16_t PCIE_EXT_CAP_ID_PROCESS_ADDRESS_SPACE_ID                = 0x001B;
constexpr uint16_t PCIE_EXT_CAP_ID_LN_REQUESTER                            = 0x001C;
constexpr uint16_t PCIE_EXT_CAP_ID_DOWNSTREAM_PORT_CONTAINMENT             = 0x001D;
constexpr uint16_t PCIE_EXT_CAP_ID_L1_PM_SUBSTATES                         = 0x001E;
constexpr uint16_t PCIE_EXT_CAP_ID_PRECISION_TIME_MEASUREMENT              = 0x001F;
constexpr uint16_t PCIE_EXT_CAP_ID_PCI_EXPRESS_OVER_MPHY                   = 0x0020;
constexpr uint16_t PCIE_EXT_CAP_ID_FRS_QUEUEING                            = 0x0021;
constexpr uint16_t PCIE_EXT_CAP_ID_READINESS_TIME_REPORTING                = 0x0022;
constexpr uint16_t PCIE_EXT_CAP_ID_DESIGNATED_VENDOR_SPECIFIC              = 0x0023;

/**
 * General PCI/PCIe capability classes. Final calculated address
 * for config corresponds to cfg's base plus cap's base along with
 * the specific register's offset.
 */
class PciStdCapability : public mxtl::SinglyLinkedListable<mxtl::unique_ptr<PciStdCapability>> {
public:
    PciStdCapability(const PcieDevice& dev, uint16_t base, uint8_t id)
        : dev_(dev), base_(base), id_(id) {}
    virtual ~PciStdCapability() {};
    uint8_t id() const { return id_; }
    uint16_t base() const { return base_; }
    bool is_valid() const { return is_valid_; }
    const PcieDevice& dev() const { return dev_; }

protected:
    const PcieDevice& dev_; /* Capabilities are owned by a device so there's no reason to worry
                             * about the device pointer's lifecycle */
    uint16_t base_;
    uint8_t  id_;
    bool     is_valid_ = false;
};

/* MSI Interrupts.
 * @see PCI Local Bus Spec v3.0 section 6.8.
 */
class PciCapMsi : public PciStdCapability {
public:
    // TODO(cja) make a cleanup pass to standardize between ALL_CAPS and kCamelCase
    static constexpr uint16_t kControlOffset       = 0x02;
    static constexpr uint16_t kAddrOffset          = 0x04;
    static constexpr uint16_t kData32Offset        = 0x08;
    static constexpr uint16_t kAddrUpperOffset     = 0x08;
    static constexpr uint16_t kData64Offset        = 0x0C;
    static constexpr uint16_t kMaskBits32Offset    = 0x0C;
    static constexpr uint16_t kPendingBits32Offset = 0x10;
    static constexpr uint16_t kMaskBits64Offset    = 0x10;
    static constexpr uint16_t kPendingBits64Offset = 0x14;
    static constexpr uint16_t k32BitNoPvmSize      = static_cast<uint16_t>(kData32Offset + 2u);
    static constexpr uint16_t k32BitPvmSize        = static_cast<uint16_t>(kMaskBits32Offset + 4u);
    static constexpr uint16_t k64BitNoPvmSize      = static_cast<uint16_t>(kData64Offset + 2u);
    static constexpr uint16_t k64BitPvmSize        = static_cast<uint16_t>(kMaskBits64Offset + 4u);

    PciCapMsi(const PcieDevice& dev, uint16_t base, uint8_t id);
    ~PciCapMsi() {}

    // Accessors
    bool is64Bit() const { return is_64_bit_; }
    bool has_pvm() const { return has_pvm_; }
    bool max_irqs() const { return max_irqs_; }
    PciReg16 ctrl_reg() const { return ctrl_; }
    PciReg32 addr_reg() const { return addr_; }
    PciReg32 addr_upper_reg() const { return addr_upper_; }
    PciReg16 data_reg() const { return data_; }
    PciReg32 mask_bits_reg() const { return mask_bits_; }
    PciReg32 pending_bits_reg() const { return pending_bits_; }
    pcie_msi_block_t irq_block() const { return irq_block_; }

private:
    // TODO(cja): Dragons here. irq_block_ is setup by PcieDevice rather than the init for
    // PciCapMsi. This should be refactored.
    friend class PcieDevice;
    uint16_t msi_size_;
    bool has_pvm_;
    bool is_64_bit_;
    unsigned int max_irqs_ = 0;
    pcie_msi_block_t irq_block_;

    // Cached registers
    PciReg16 ctrl_;
    PciReg32 addr_;
    PciReg32 addr_upper_;
    PciReg16 data_;
    PciReg32 mask_bits_;
    PciReg32 pending_bits_;
};

/* PCI Express Capability classes */

class PciCapPcie : public PciStdCapability {
 private:
    class PcieCapChunk {
        public:
        PciReg32 caps() const   { return caps_; }
        PciReg16 ctrl() const   { return ctrl_; }
        PciReg16 status() const { return status_; }

        private:
        friend class PciCapPcie;
        PciReg32 caps_;
        PciReg16 ctrl_;
        PciReg16 status_;
    };

    class PcieCapRootChunk {
        public:
        PciReg16 caps() const   { return caps_; }
        PciReg16 ctrl() const   { return ctrl_; }
        PciReg32 status() const { return status_; }

        private:
        friend class PciCapPcie;
        PciReg16 caps_;
        PciReg16 ctrl_;
        PciReg32 status_;
    };

 public:
    // Primary grouping offsets
    static constexpr uint16_t kPcieCapsOffset = 0x02;
    static constexpr uint16_t kDeviceOffset   = 0x04;
    static constexpr uint16_t kLinkOffset     = 0x0C;
    static constexpr uint16_t kSlotOffset     = 0x14;
    static constexpr uint16_t kRootOffset     = 0x1C;
    static constexpr uint16_t kDevice2Offset  = 0x24;
    static constexpr uint16_t kLink2Offset    = 0x2C;
    static constexpr uint16_t kSlot2Offset    = 0x34;

    // Root is laid out differently so it gets specific definitions
    static constexpr uint16_t kRootControlOffset = 0x1C;
    static constexpr uint16_t kRootCapsOffset    = 0x1E;
    static constexpr uint16_t kRootStatusOffset  = 0x20;

    PciCapPcie(const PcieDevice& dev, uint16_t base, uint8_t id);
    ~PciCapPcie() {}

    pcie_device_type_t devtype() const { return devtype_; }
    uint8_t version() const { return version_; }
    bool has_flr() const { return has_flr_; }
    // For Device, Link, and Slot.
    uint16_t kCapsOffset(uint16_t base) const { return static_cast<uint16_t>(base + 0x0); }
    uint16_t kControlOffset(uint16_t base) const { return static_cast<uint16_t>(base + 0x4); }
    uint16_t kStatusOffset(uint16_t base) const { return static_cast<uint16_t>(base + 0x6); }
    PciReg16 caps() const { return caps_; }

    PcieCapChunk     device;
    PcieCapChunk     link;
    PcieCapChunk     slot;

    PcieCapRootChunk root;

    PcieCapChunk     device2;
    PcieCapChunk     link2;
    PcieCapChunk     slot2;

 protected:
    uint8_t            version_;
    pcie_device_type_t devtype_ = PCIE_DEVTYPE_UNKNOWN;
    PciReg16           caps_;
    bool               has_flr_;
};

class PciCapAdvFeatures : public PciStdCapability {
 public:
    static constexpr uint16_t kLengthOffset    = 0x2;
    static constexpr uint16_t kAFCapsOffset    = 0x3;
    static constexpr uint16_t kAFControlOffset = 0x4;
    static constexpr uint16_t kAFStatusOffset  = 0x5;

    PciCapAdvFeatures(const PcieDevice& dev, uint16_t base, uint8_t id);
    ~PciCapAdvFeatures() {}

    bool has_flr() const { return has_flr_; }
    bool has_tp() const { return has_tp_; }
    PciReg8 length() const { return length_; }
    PciReg8 af_caps() const { return af_caps_; }
    PciReg8 af_ctrl() const { return af_ctrl_; }
    PciReg8 af_status() const { return af_status_; }

 private:
    bool has_flr_; // Supports Function Level Reset
    bool has_tp_;  // Supports Transactions Pending

    /* Capability registers mapped */
    PciReg8 length_;
    PciReg8 af_caps_;
    PciReg8 af_ctrl_;
    PciReg8 af_status_;
};
