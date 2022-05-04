// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the fuchsia.hardware.pci banjo file

#ifndef SRC_DEVICES_PCI_LIB_FUCHSIA_HARDWARE_PCI_INCLUDE_FUCHSIA_HARDWARE_PCI_C_BANJO_H_
#define SRC_DEVICES_PCI_LIB_FUCHSIA_HARDWARE_PCI_INCLUDE_FUCHSIA_HARDWARE_PCI_C_BANJO_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef uint16_t pci_status_t;
#define PCI_STATUS_INTERRUPT UINT16_C(0x8)
#define PCI_STATUS_NEW_CAPS UINT16_C(0x10)
#define PCI_STATUS_SIXTYSIX_MHZ UINT16_C(0x20)
#define PCI_STATUS_FAST_B2B UINT16_C(0x80)
#define PCI_STATUS_MSTR_PERR UINT16_C(0x100)
#define PCI_STATUS_DEVSEL_LOW UINT16_C(0x200)
#define PCI_STATUS_DEVSEL_HIGH UINT16_C(0x400)
#define PCI_STATUS_TARG_ABORT_SIG UINT16_C(0x800)
#define PCI_STATUS_TARG_ABORT_RCV UINT16_C(0x1000)
#define PCI_STATUS_MSTR_ABORT_RCV UINT16_C(0x2000)
#define PCI_STATUS_SERR_SIG UINT16_C(0x4000)
#define PCI_STATUS_PERR UINT16_C(0x8000)
#define PCI_STATUS_DEVSEL_MASK UINT16_C(1536)
typedef struct pci_io_bar pci_io_bar_t;
typedef union pci_bar_result pci_bar_result_t;
typedef struct pci_interrupt_modes pci_interrupt_modes_t;
typedef uint8_t pci_interrupt_mode_t;
#define PCI_INTERRUPT_MODE_DISABLED UINT8_C(0)
#define PCI_INTERRUPT_MODE_LEGACY UINT8_C(1)
#define PCI_INTERRUPT_MODE_LEGACY_NOACK UINT8_C(2)
#define PCI_INTERRUPT_MODE_MSI UINT8_C(3)
#define PCI_INTERRUPT_MODE_MSI_X UINT8_C(4)
#define PCI_INTERRUPT_MODE_COUNT UINT8_C(5)
typedef uint8_t pci_header_type_t;
#define PCI_HEADER_TYPE_STANDARD UINT8_C(0x0)
#define PCI_HEADER_TYPE_BRIDGE UINT8_C(0x1)
#define PCI_HEADER_TYPE_CARD_BUS UINT8_C(0x2)
#define PCI_HEADER_TYPE_MASK UINT8_C(0x7F)
#define PCI_HEADER_TYPE_MULTI_FN UINT8_C(0x80)
typedef uint16_t pci_extended_config_offset_t;
typedef uint16_t pci_extended_capability_id_t;
#define PCI_EXTENDED_CAPABILITY_ID_NULL UINT16_C(0x00)
#define PCI_EXTENDED_CAPABILITY_ID_ADVANCED_ERROR_REPORTING UINT16_C(0x01)
#define PCI_EXTENDED_CAPABILITY_ID_VIRTUAL_CHANNEL_NO_MFVC UINT16_C(0x02)
#define PCI_EXTENDED_CAPABILITY_ID_DEVICE_SERIAL_NUMBER UINT16_C(0x03)
#define PCI_EXTENDED_CAPABILITY_ID_POWER_BUDGETING UINT16_C(0x04)
#define PCI_EXTENDED_CAPABILITY_ID_ROOT_COMPLEX_LINK_DECLARATION UINT16_C(0x05)
#define PCI_EXTENDED_CAPABILITY_ID_ROOT_COMPLEX_INTERNAL_LINK_CONTROL UINT16_C(0x06)
#define PCI_EXTENDED_CAPABILITY_ID_ROOT_COMPLEX_EVENT_COLLECTOR_ENDPOINT_ASSOCIATION UINT16_C(0x07)
#define PCI_EXTENDED_CAPABILITY_ID_MULTI_FUNCTION_VIRTUAL_CHANNEL UINT16_C(0x08)
#define PCI_EXTENDED_CAPABILITY_ID_VIRTUAL_CHANNEL UINT16_C(0x09)
#define PCI_EXTENDED_CAPABILITY_ID_RCRB UINT16_C(0x0a)
#define PCI_EXTENDED_CAPABILITY_ID_VENDOR UINT16_C(0x0b)
#define PCI_EXTENDED_CAPABILITY_ID_CAC UINT16_C(0x0c)
#define PCI_EXTENDED_CAPABILITY_ID_ACS UINT16_C(0x0d)
#define PCI_EXTENDED_CAPABILITY_ID_ARI UINT16_C(0x0e)
#define PCI_EXTENDED_CAPABILITY_ID_ATS UINT16_C(0x0f)
#define PCI_EXTENDED_CAPABILITY_ID_SR_IOV UINT16_C(0x10)
#define PCI_EXTENDED_CAPABILITY_ID_MR_IOV UINT16_C(0x11)
#define PCI_EXTENDED_CAPABILITY_ID_MULTICAST UINT16_C(0x12)
#define PCI_EXTENDED_CAPABILITY_ID_PRI UINT16_C(0x13)
#define PCI_EXTENDED_CAPABILITY_ID_ENHANCED_ALLOCATION UINT16_C(0x14)
#define PCI_EXTENDED_CAPABILITY_ID_RESIZABLE_BAR UINT16_C(0x15)
#define PCI_EXTENDED_CAPABILITY_ID_DYNAMIC_POWER_ALLOCATION UINT16_C(0x16)
#define PCI_EXTENDED_CAPABILITY_ID_TPH UINT16_C(0x17)
#define PCI_EXTENDED_CAPABILITY_ID_LATENCY_TOLERANCE_REPORTING UINT16_C(0x18)
#define PCI_EXTENDED_CAPABILITY_ID_SECONDARY_PCI_EXPRESS UINT16_C(0x19)
#define PCI_EXTENDED_CAPABILITY_ID_PMUX UINT16_C(0x1a)
#define PCI_EXTENDED_CAPABILITY_ID_PASID UINT16_C(0x1b)
#define PCI_EXTENDED_CAPABILITY_ID_LNR UINT16_C(0x1c)
#define PCI_EXTENDED_CAPABILITY_ID_DPC UINT16_C(0x1d)
#define PCI_EXTENDED_CAPABILITY_ID_L1PM_SUBSTATES UINT16_C(0x1e)
#define PCI_EXTENDED_CAPABILITY_ID_PRECISION_TIME_MEASUREMENT UINT16_C(0x1f)
#define PCI_EXTENDED_CAPABILITY_ID_MPCIE UINT16_C(0x20)
#define PCI_EXTENDED_CAPABILITY_ID_FRS_QUEUEING UINT16_C(0x21)
#define PCI_EXTENDED_CAPABILITY_ID_READINESS_TIME_REPORTING UINT16_C(0x22)
#define PCI_EXTENDED_CAPABILITY_ID_DESIGNATED_VENDOR UINT16_C(0x23)
#define PCI_EXTENDED_CAPABILITY_ID_VF_RESIZABLE_BAR UINT16_C(0x24)
#define PCI_EXTENDED_CAPABILITY_ID_DATA_LINK_FEATURE UINT16_C(0x25)
#define PCI_EXTENDED_CAPABILITY_ID_PHYSICAL_LAYER_16 UINT16_C(0x26)
#define PCI_EXTENDED_CAPABILITY_ID_LANE_MARGINING_AT_RECEIVER UINT16_C(0x27)
#define PCI_EXTENDED_CAPABILITY_ID_HIERARCHY_ID UINT16_C(0x28)
#define PCI_EXTENDED_CAPABILITY_ID_NATIVE_PCIE_ENCLOSURE UINT16_C(0x29)
#define PCI_EXTENDED_CAPABILITY_ID_PHYSICAL_LAYER_32 UINT16_C(0x2a)
#define PCI_EXTENDED_CAPABILITY_ID_ALTERNATE_PROTOCOL UINT16_C(0x2b)
#define PCI_EXTENDED_CAPABILITY_ID_SYSTEM_FIRMWARE_INTERMEDIARY UINT16_C(0x2c)
typedef struct pci_device_info pci_device_info_t;
typedef uint8_t pci_config_offset_t;
typedef uint16_t pci_config_t;
#define PCI_CONFIG_VENDOR_ID UINT16_C(0x00)
#define PCI_CONFIG_DEVICE_ID UINT16_C(0x02)
#define PCI_CONFIG_COMMAND UINT16_C(0x04)
#define PCI_CONFIG_STATUS UINT16_C(0x06)
#define PCI_CONFIG_REVISION_ID UINT16_C(0x08)
#define PCI_CONFIG_CLASS_CODE_INTR UINT16_C(0x09)
#define PCI_CONFIG_CLASS_CODE_SUB UINT16_C(0x0a)
#define PCI_CONFIG_CLASS_CODE_BASE UINT16_C(0x0b)
#define PCI_CONFIG_CACHE_LINE_SIZE UINT16_C(0x0c)
#define PCI_CONFIG_LATENCY_TIMER UINT16_C(0x0d)
#define PCI_CONFIG_HEADER_TYPE UINT16_C(0x0e)
#define PCI_CONFIG_BIST UINT16_C(0x0f)
#define PCI_CONFIG_BASE_ADDRESSES UINT16_C(0x10)
#define PCI_CONFIG_CARDBUS_CIS_PTR UINT16_C(0x28)
#define PCI_CONFIG_SUBSYSTEM_VENDOR_ID UINT16_C(0x2c)
#define PCI_CONFIG_SUBSYSTEM_ID UINT16_C(0x2e)
#define PCI_CONFIG_EXP_ROM_ADDRESS UINT16_C(0x30)
#define PCI_CONFIG_CAPABILITIES_PTR UINT16_C(0x34)
#define PCI_CONFIG_INTERRUPT_LINE UINT16_C(0x3c)
#define PCI_CONFIG_INTERRUPT_PIN UINT16_C(0x3d)
#define PCI_CONFIG_MIN_GRANT UINT16_C(0x3e)
#define PCI_CONFIG_MAX_LATENCY UINT16_C(0x3f)
typedef uint16_t pci_command_t;
#define PCI_COMMAND_IO_EN UINT16_C(0x1)
#define PCI_COMMAND_MEM_EN UINT16_C(0x2)
#define PCI_COMMAND_BUS_MASTER_EN UINT16_C(0x4)
#define PCI_COMMAND_SPECIAL_EN UINT16_C(0x8)
#define PCI_COMMAND_MEM_WR_INV_EN UINT16_C(0x10)
#define PCI_COMMAND_PAL_SNOOP_EN UINT16_C(0x20)
#define PCI_COMMAND_PERR_RESP_EN UINT16_C(0x40)
#define PCI_COMMAND_AD_STEP_EN UINT16_C(0x80)
#define PCI_COMMAND_SERR_EN UINT16_C(0x100)
#define PCI_COMMAND_FAST_B2B_EN UINT16_C(0x200)
typedef uint8_t pci_capability_id_t;
#define PCI_CAPABILITY_ID_NULL UINT8_C(0x00)
#define PCI_CAPABILITY_ID_PCI_PWR_MGMT UINT8_C(0x01)
#define PCI_CAPABILITY_ID_AGP UINT8_C(0x02)
#define PCI_CAPABILITY_ID_VITAL_PRODUCT_DATA UINT8_C(0x03)
#define PCI_CAPABILITY_ID_SLOT_IDENTIFICATION UINT8_C(0x04)
#define PCI_CAPABILITY_ID_MSI UINT8_C(0x05)
#define PCI_CAPABILITY_ID_COMPACT_PCI_HOTSWAP UINT8_C(0x06)
#define PCI_CAPABILITY_ID_PCIX UINT8_C(0x07)
#define PCI_CAPABILITY_ID_HYPERTRANSPORT UINT8_C(0x08)
#define PCI_CAPABILITY_ID_VENDOR UINT8_C(0x09)
#define PCI_CAPABILITY_ID_DEBUG_PORT UINT8_C(0x0a)
#define PCI_CAPABILITY_ID_COMPACT_PCI_CRC UINT8_C(0x0b)
#define PCI_CAPABILITY_ID_PCI_HOT_PLUG UINT8_C(0x0c)
#define PCI_CAPABILITY_ID_PCI_BRIDGE_SUBSYSTEM_VID UINT8_C(0x0d)
#define PCI_CAPABILITY_ID_AGP8X UINT8_C(0x0e)
#define PCI_CAPABILITY_ID_SECURE_DEVICE UINT8_C(0x0f)
#define PCI_CAPABILITY_ID_PCI_EXPRESS UINT8_C(0x10)
#define PCI_CAPABILITY_ID_MSIX UINT8_C(0x11)
#define PCI_CAPABILITY_ID_SATA_DATA_NDX_CFG UINT8_C(0x12)
#define PCI_CAPABILITY_ID_ADVANCED_FEATURES UINT8_C(0x13)
#define PCI_CAPABILITY_ID_ENHANCED_ALLOCATION UINT8_C(0x14)
#define PCI_CAPABILITY_ID_FLATTENING_PORTAL_BRIDGE UINT8_C(0x15)
typedef uint8_t pci_bar_type_t;
#define PCI_BAR_TYPE_UNUSED UINT8_C(0)
#define PCI_BAR_TYPE_MMIO UINT8_C(1)
#define PCI_BAR_TYPE_IO UINT8_C(2)
typedef struct pci_bar pci_bar_t;
typedef struct pci_protocol pci_protocol_t;
typedef struct pci_protocol_ops pci_protocol_ops_t;
#define PCI_MAX_BAR_COUNT UINT8_C(6)

// Declarations
struct pci_io_bar {
  uint64_t address;
  zx_handle_t resource;
};

union pci_bar_result {
  pci_io_bar_t io;
  zx_handle_t vmo;
};

// Returned by |GetInterruptModes|. Contains the number of interrupts supported
// by a given PCI device interrupt mode. 0 is returned for a mode if
// unsupported.
struct pci_interrupt_modes {
  // |True| if the device supports a legacy interrupt.
  bool has_legacy;
  // The number of Message-Signaled interrupted supported. Will be in the
  // range of [0, 0x8) depending on device support.
  uint8_t msi_count;
  // The number of MSI-X interrupts supported. Will be in the range of [0,
  // 0x800), depending on device and platform support.
  uint16_t msix_count;
};

// Device specific information from a device's configuration header.
// PCI Local Bus Specification v3, chapter 6.1.
struct pci_device_info {
  // Device identification information.
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t base_class;
  uint8_t sub_class;
  uint8_t program_interface;
  uint8_t revision_id;
  // Information pertaining to the device's location in the bus topology.
  uint8_t bus_id;
  uint8_t dev_id;
  uint8_t func_id;
  uint8_t padding1;
};

// Describes and provides access to a given Base Address Register for the device.
struct pci_bar {
  // The BAR id, [0-5).
  uint32_t bar_id;
  uint64_t size;
  pci_bar_type_t type;
  pci_bar_result_t result;
};

struct pci_protocol_ops {
  zx_status_t (*get_device_info)(void* ctx, pci_device_info_t* out_info);
  zx_status_t (*get_bar)(void* ctx, uint32_t bar_id, pci_bar_t* out_result);
  zx_status_t (*set_bus_mastering)(void* ctx, bool enabled);
  zx_status_t (*reset_device)(void* ctx);
  zx_status_t (*ack_interrupt)(void* ctx);
  zx_status_t (*map_interrupt)(void* ctx, uint32_t which_irq, zx_handle_t* out_interrupt);
  void (*get_interrupt_modes)(void* ctx, pci_interrupt_modes_t* out_modes);
  zx_status_t (*set_interrupt_mode)(void* ctx, pci_interrupt_mode_t mode,
                                    uint32_t requested_irq_count);
  zx_status_t (*read_config8)(void* ctx, uint16_t offset, uint8_t* out_value);
  zx_status_t (*read_config16)(void* ctx, uint16_t offset, uint16_t* out_value);
  zx_status_t (*read_config32)(void* ctx, uint16_t offset, uint32_t* out_value);
  zx_status_t (*write_config8)(void* ctx, uint16_t offset, uint8_t value);
  zx_status_t (*write_config16)(void* ctx, uint16_t offset, uint16_t value);
  zx_status_t (*write_config32)(void* ctx, uint16_t offset, uint32_t value);
  zx_status_t (*get_first_capability)(void* ctx, pci_capability_id_t id, uint8_t* out_offset);
  zx_status_t (*get_next_capability)(void* ctx, pci_capability_id_t id, uint8_t start_offset,
                                     uint8_t* out_offset);
  zx_status_t (*get_first_extended_capability)(void* ctx, pci_extended_capability_id_t id,
                                               uint16_t* out_offset);
  zx_status_t (*get_next_extended_capability)(void* ctx, pci_extended_capability_id_t id,
                                              uint16_t start_offset, uint16_t* out_offset);
  zx_status_t (*get_bti)(void* ctx, uint32_t index, zx_handle_t* out_bti);
};

struct pci_protocol {
  pci_protocol_ops_t* ops;
  void* ctx;
};

// Helpers
// Returns a structure containing device information from the configuration header.
static inline zx_status_t pci_get_device_info(const pci_protocol_t* proto,
                                              pci_device_info_t* out_info) {
  return proto->ops->get_device_info(proto->ctx, out_info);
}

// Retrieves information for a specified Base Address Register.
//
// Parameters:
// |bar_id|: The id of the BAR being requested. Valid range is [0, 6).
//
// Errors:
// |ZX_ERR_INTERNAL|: A bus driver error has occurred.
// |ZX_ERR_INVALID_ARGS|: The |bar_id| specified is outside of the acceptable range.
// |ZX_ERR_NOT_FOUND|: The specified |bar_id| does not exist for this device.
static inline zx_status_t pci_get_bar(const pci_protocol_t* proto, uint32_t bar_id,
                                      pci_bar_t* out_result) {
  return proto->ops->get_bar(proto->ctx, bar_id, out_result);
}

// Enables or disables the bus mastering capability for the device.
//
// Parameters:
// |enable|: true to enable bus mastering, false to disable.
//
// Errors:
// |ZX_ERR_BAD_STATE|: Method was called while the device is disabled.
static inline zx_status_t pci_set_bus_mastering(const pci_protocol_t* proto, bool enabled) {
  return proto->ops->set_bus_mastering(proto->ctx, enabled);
}

// Initiates a function level reset for the device. This is a synchronous
// operation that will not return ontil the reset is complete. Interrupt
// operation of the device must be disabled before initiating a reset.
//
// Errors:
// |ZX_ERR_BAD_STATE|: Interrupts were not disabled before calling |ResetDevice|.
// |ZX_ERR_NOT_SUPPORTED|: The device does not support reset.
// |ZX_ERR_TIMED_OUT|: The device did not complete its reset in the
// expected amount of time and is presumed to no longer be operating
// properly.
static inline zx_status_t pci_reset_device(const pci_protocol_t* proto) {
  return proto->ops->reset_device(proto->ctx);
}

// Alerts the bus driver to deassert the raised legacy interrupt so that it
// may be waited on again. Only used if |SetInterruptMode| was called with
// |PCI_INTERRUPT_MODE_LEGACY|.
//
// Errors:
// |ZX_ERR_BAD_STATE|: device is not configured to use the Legacy interrupt mode.
static inline zx_status_t pci_ack_interrupt(const pci_protocol_t* proto) {
  return proto->ops->ack_interrupt(proto->ctx);
}

// Maps a device's interrupt to a zx:interrupt. The device's interrupt mode
// must already be configured with |SetInterruptMode|, and |which_irq| must
// be >= to the number of interrupts reported for that interrupt mode by
// |GetInterruptModes|. A Legacy interrupt may be mapped multiple times,
// but the handles will point to the same interrupt object. MSI & MSI-X
// interrupts may only have one outstanding mapping at a time per
// interrupt. Outstanding MSI & MSI-X interrupt handles must be closed
// before attempting to change the interrupt mode in a subsequent call to
// |SetInterruptMode|.
//
// Parameters:
// |which_irq|: The id of the interrupt to map.
//
// Errors:
// |ZX_ERR_ALREADY_BOUND|: The interrupt specified by |which_irq| is
// already mapped to a valid handle.
// |ZX_ERR_BAD_STATE|: interrupts are currently disabled for the device.
// |ZX_ERR_INVALID_ARGS|: |which_irq| is invalid for the mode.
static inline zx_status_t pci_map_interrupt(const pci_protocol_t* proto, uint32_t which_irq,
                                            zx_handle_t* out_interrupt) {
  return proto->ops->map_interrupt(proto->ctx, which_irq, out_interrupt);
}

// Returns the supported interrupt modes for a device.
static inline void pci_get_interrupt_modes(const pci_protocol_t* proto,
                                           pci_interrupt_modes_t* out_modes) {
  proto->ops->get_interrupt_modes(proto->ctx, out_modes);
}

// Configures the interrupt mode for a device. When changing from one
// interrupt mode to another the driver must ensure existing interrupt
// handles are closed beforehand.
//
// Parameters:
// |mode|: The |PciInterruptMode| to request from the bus driver.
// |requested_irq_count|: The number of interrupts requested.
//
// Errors:
// |ZX_ERR_BAD_STATE|: The driver attempted to change interrupt mode while
// existing handles to mapped MSIs exist.
// |ZX_ERR_INVALID_ARGS|: |requested_irq_count| is 0.
// |ZX_ERR_NOT_SUPPORTED|: The provided |mode| is not supported, or invalid.
static inline zx_status_t pci_set_interrupt_mode(const pci_protocol_t* proto,
                                                 pci_interrupt_mode_t mode,
                                                 uint32_t requested_irq_count) {
  return proto->ops->set_interrupt_mode(proto->ctx, mode, requested_irq_count);
}

// Reads a byte from the device's configuration space. |Offset| must be
// within [0x0, 0xFF] if PCI, or [0x0, 0xFFF) if PCIe. In most cases a
// device will be PCIe.
//
// Parameters:
// |offset|: The offset into the device's configuration space to read.
//
// Errors:
// |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
static inline zx_status_t pci_read_config8(const pci_protocol_t* proto, uint16_t offset,
                                           uint8_t* out_value) {
  return proto->ops->read_config8(proto->ctx, offset, out_value);
}

// Reads two bytes from the device's configuration space. |Offset| must be
// within [0x0, 0xFE] if PCI, or [0x0, 0xFFE] if PCIe. In most cases a
// device will be PCIe.
//
// Parameters:
// |offset|: The offset into the device's configuration space to read.
//
// Errors:
// |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
static inline zx_status_t pci_read_config16(const pci_protocol_t* proto, uint16_t offset,
                                            uint16_t* out_value) {
  return proto->ops->read_config16(proto->ctx, offset, out_value);
}

// Reads four bytes from the device's configuration space. |Offset| must be
// within [0x0, 0xFC] if PCI, or [0x0, 0xFFC] if PCIe. In most cases a
// device will be PCIe.
//
// Parameters:
// |offset|: The offset into the device's configuration space to read.
//
// Errors:
// |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
static inline zx_status_t pci_read_config32(const pci_protocol_t* proto, uint16_t offset,
                                            uint32_t* out_value) {
  return proto->ops->read_config32(proto->ctx, offset, out_value);
}

// Writes a byte to the device's configuration space. The acceptable
// ranges of |offset| for writes are [0x40, 0xFF] if PCI, or [0x40,
// 0xFFF] if PCIe. For most purposes a device will be PCIe.
//
//
// Parameters
// |offset|: The offset into the device's configuration space to read.
// |value|: The value to write.
//
// Errors:
// |ZX_ERR_ACCESS_DENIED|: |offset| is within the device's configuration header.
// |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
static inline zx_status_t pci_write_config8(const pci_protocol_t* proto, uint16_t offset,
                                            uint8_t value) {
  return proto->ops->write_config8(proto->ctx, offset, value);
}

// Writes two bytes to the device's configuration space. The acceptable
// ranges of |offset| for writes are [0x40, 0xFE] if PCI, or [0x40,
// 0xFFE] if PCIe. For most purposes a device will be PCIe.
//
//
// Parameters
// |offset|: The offset into the device's configuration space to read.
// |value|: The value to write.
//
// Errors:
// |ZX_ERR_ACCESS_DENIED|: |offset| is within the device's configuration header.
// |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
static inline zx_status_t pci_write_config16(const pci_protocol_t* proto, uint16_t offset,
                                             uint16_t value) {
  return proto->ops->write_config16(proto->ctx, offset, value);
}

// Writes four bytes to the device's configuration space. The acceptable
// ranges of |offset| for writes are [0x40, 0xFC] if PCI, or [0x40,
// 0xFFC] if PCIe. For most purposes a device will be PCIe.
//
//
// Parameters
// |offset|: The offset into the device's configuration space to read.
// |value|: The value to write.
//
// Errors:
// |ZX_ERR_ACCESS_DENIED|: |offset| is within the device's configuration header.
// |ZX_ERR_OUT_OF_RANGE|: |offset| is an invalid address.
static inline zx_status_t pci_write_config32(const pci_protocol_t* proto, uint16_t offset,
                                             uint32_t value) {
  return proto->ops->write_config32(proto->ctx, offset, value);
}

// Returns the offset into the device's configuration space of the first
// capability matching the capability id.
//
// Parameters:
// |id|: the capability id to search for.
//
// Errors:
// |ZX_ERR_NOT_FOUND|: A capability of id |id| was not found.
static inline zx_status_t pci_get_first_capability(const pci_protocol_t* proto,
                                                   pci_capability_id_t id, uint8_t* out_offset) {
  return proto->ops->get_first_capability(proto->ctx, id, out_offset);
}

// Returns the offset into the device's configuration space of the next
// capability matching the provided capability id, starting at |offset|.
//
// Parameters:
// |id|: the capability id to search for.
// |start_offset|: the configuration space offset of the previous
// capability to start searching from.
//
// Errors:
// |ZX_ERR_NOT_FOUND|: A capability of id |id| was not found in a scan
// starting from |offset|
static inline zx_status_t pci_get_next_capability(const pci_protocol_t* proto,
                                                  pci_capability_id_t id, uint8_t start_offset,
                                                  uint8_t* out_offset) {
  return proto->ops->get_next_capability(proto->ctx, id, start_offset, out_offset);
}

// Returns the offset into the device's configuration space of first
// extended capability matching the provided extended capability id.
//
// Parameters:
// |id|: the capability id to search for
//
// Errors:
// |ZX_ERR_NOT_FOUND|: A extended capability of id |id| was not found.
static inline zx_status_t pci_get_first_extended_capability(const pci_protocol_t* proto,
                                                            pci_extended_capability_id_t id,
                                                            uint16_t* out_offset) {
  return proto->ops->get_first_extended_capability(proto->ctx, id, out_offset);
}

// Returns the offset into the device's configuration space of the next
// extended capability matching the provided extended capability id,
// starting at |offset|.
//
// Parameters:
// |id|: the capability id to search for.
// |start_offset|: the configuration space offset of the previous extended
// capability to start searching from.
//
// Errors
// |ZX_ERR_NOT_FOUND|: A extended capability of id |id| was not found in a
// scan starting from |offset|.
static inline zx_status_t pci_get_next_extended_capability(const pci_protocol_t* proto,
                                                           pci_extended_capability_id_t id,
                                                           uint16_t start_offset,
                                                           uint16_t* out_offset) {
  return proto->ops->get_next_extended_capability(proto->ctx, id, start_offset, out_offset);
}

// Returns the Bus Transaction Intiator (BTI) at a given index for the device.
//
// Parameters:
// |index|: the BTI to request.
//
// Errors:
// |ZX_ERR_OUT_OF_RANGE|: |index| was not 0.
static inline zx_status_t pci_get_bti(const pci_protocol_t* proto, uint32_t index,
                                      zx_handle_t* out_bti) {
  return proto->ops->get_bti(proto->ctx, index, out_bti);
}

__END_CDECLS

#endif  // SRC_DEVICES_PCI_LIB_FUCHSIA_HARDWARE_PCI_INCLUDE_FUCHSIA_HARDWARE_PCI_C_BANJO_H_
