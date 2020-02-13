// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_XDC_HW_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_XDC_HW_H_

#include <assert.h>

#include "xhci-hw.h"

namespace usb_xhci {

// clang-format off

// Debug Capability Structure (XHCI Spec, Table 7-16, p. 526)
typedef volatile struct {
    uint32_t dcid;          // Capability ID
    uint32_t dcdb;          // Doorbell

    // Event Ring Management
    uint32_t dcerstsz;      // Event Ring Segment Table Size
    uint32_t reserved1;
    uint64_t dcerstba;      // Event Ring Segment Table Base Address
    uint64_t dcerdp;        // Event Ring Dequeue Pointer

    uint32_t dcctrl;        // Control
    uint32_t dcst;          // Status

    // Port Management
    uint32_t dcportsc;      // Port Status and Control

    uint32_t reserved2;

    // Endpoint Management
    uint64_t dccp;          // Debug Capability Context Pointer

    // Device Descriptor Information
    uint32_t dcddi1;        // Device Descriptor Info Register 1
    uint32_t dcddi2;        // Device Descriptor Info Register 2
} __PACKED xdc_debug_cap_regs_t;

static_assert(sizeof(xdc_debug_cap_regs_t) == 0x40, "xdc debug cap wrong size");

// Debug Capability Info Context (DbCIC) Data Structure (XHCI Spec, Figure 7-11, p.537)
typedef struct {
    uint64_t str_0_desc_addr;
    uint64_t manufacturer_desc_addr;
    uint64_t product_desc_addr;
    uint64_t serial_num_desc_addr;

    uint8_t str_0_desc_len;
    uint8_t manufacturer_desc_len;
    uint8_t product_desc_len;
    uint8_t serial_num_desc_len;

    uint32_t reserved[7];
} __PACKED xdc_dbcic_t;

static_assert(sizeof(xdc_dbcic_t) == 0x40, "xdc dbcic wrong size");

// Debug Capability Context Data Structure (XHCI Spec, Figure 7-10, p. 536)
typedef struct {
    xdc_dbcic_t dbcic;

    // These are the 64-byte versions of an Endpoint Context.
    // They have an extra 32 bytes reserved.
    xhci_endpoint_context_t out_epc;
    uint32_t reserved1[8];

    xhci_endpoint_context_t in_epc;
    uint32_t reserved2[8];
} __PACKED xdc_context_data_t;

static_assert(sizeof(xdc_context_data_t) == 0xC0, "xdc context data wrong size");

// Debug Capability Doorbell Register (DCDB) bits
#define DCDB_DB_START                     8
#define DCDB_DB_BITS                      8

// Doorbell values to write to the DCDB.
#define DCDB_DB_EP_OUT                    0
#define DCDB_DB_EP_IN                     1

// Debug Capability Control Register (DCCTRL) bits
#define DCCTRL_DCR                   (1 << 0)
#define DCCTRL_LSE                   (1 << 1)
#define DCCTRL_HOT                   (1 << 2)
#define DCCTRL_HIT                   (1 << 3)
#define DCCTRL_DRC                   (1 << 4)
#define DCCTRL_MAX_BURST_START       16
#define DCCTRL_MAX_BURST_BITS        8
#define DCCTRL_DCE                   (1 << 31)

// Debug Capability Status Register (DCST) bits
#define DCST_ER_NOT_EMPTY_START           0
#define DCST_ER_NOT_EMPTY_BITS            1
#define DCST_PORT_NUM_START               24
#define DCST_PORT_NUM_BITS                8

// Debug Capability Port Status and Control Register (DCPORTSC) bits
#define DCPORTSC_CCS                      (1 << 0)
#define DCPORTSC_PED                      (1 << 1)
#define DCPORTSC_PR                       (1 << 4)
#define DCPORTSC_PLS_START                5
#define DCPORTSC_PLS_BITS                 4
#define DCPORTSC_PS_START                 10
#define DCPORTSC_PS_BITS                  4
#define DCPORTSC_CSC                      (1 << 17)
#define DCPORTSC_PRC                      (1 << 21)
#define DCPORTSC_PLC                      (1 << 22)
#define DCPORTSC_CEC                      (1 << 23)

// Debug Capability Device Descriptor Info Register 1 (DCDDI1) bits
#define DCDDI1_VENDOR_ID_START            16
#define DCDDI1_VENDOR_ID_BITS             16

// Debug Capability Device Descriptor Info Register 2 (DCDDI2) bits
#define DCDDI2_PRODUCT_ID_START           0
#define DCDDI2_PRODUCT_ID_BITS            16
#define DCDDI2_DEVICE_REVISION_START      16
#define DCDDI2_DEVICE_REVISION_BITS       16

// Device Context Index for the bulk endpoint TRBs.
#define EP_OUT_DEV_CTX_IDX                2
#define EP_IN_DEV_CTX_IDX                 3

} // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_XDC_HW_H_
