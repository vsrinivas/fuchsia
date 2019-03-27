// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_HW_USB_DFU_H_
#define SYSROOT_ZIRCON_HW_USB_DFU_H_

// clang-format off

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// USB DFU Spec, Rev 1.1

// DFU Class-Specific Request Values
// Table 3.2
#define USB_DFU_DETACH     0x00
#define USB_DFU_DNLOAD     0x01
#define USB_DFU_UPLOAD     0x02
#define USB_DFU_GET_STATUS 0x03
#define USB_DFU_CLR_STATUS 0x04
#define USB_DFU_GET_STATE  0x05
#define USB_DFU_ABORT      0x06

// DFU Class-Specific Descriptor Types
// Table 4.1.3
#define USB_DFU_CS_FUNCTIONAL 0x21

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;  // USB_DFU_CS_FUNCTIONAL
    uint8_t bmAttributes;
    uint16_t wDetachTimeOut;
    uint16_t wTransferSize;
    uint16_t bcdDFUVersion;
} __PACKED usb_dfu_func_desc_t;

// DFU_GET_STATUS Response
// Section 6.1.2
typedef struct {
    uint8_t bStatus;
    uint8_t bwPollTimeout[3];  // 24 bit unsigned integer
    uint8_t bState;
    uint8_t bString;
} __PACKED usb_dfu_get_status_data_t;

// DFU Device Status Values
#define USB_DFU_STATUS_OK                     0x00
#define USB_DFU_STATUS_ERR_TARGET             0x01
#define USB_DFU_STATUS_ERR_FILE               0x02
#define USB_DFU_STATUS_ERR_WRITE              0x03
#define USB_DFU_STATUS_ERR_ERASE              0x04
#define USB_DFU_STATUS_ERR_CHECK_ERASED       0x05
#define USB_DFU_STATUS_ERR_PROG               0x06
#define USB_DFU_STATUS_ERR_VERIFY             0x07
#define USB_DFU_STATUS_ERR_ADDRESS            0x08
#define USB_DFU_STATUS_ERR_NOT_DONE           0x09
#define USB_DFU_STATUS_ERR_FIRMWARE           0x0A
#define USB_DFU_STATUS_ERR_VENDOR             0x0B
#define USB_DFU_STATUS_ERR_USER               0x0C
#define USB_DFU_STATUS_ERR_POR                0x0D
#define USB_DFU_STATUS_ERR_UNKNOWN            0x0E
#define USB_DFU_STATUS_ERR_STALLED_PKT        0x0F

// DFU Device State Values
#define USB_DFU_STATE_APP_IDLE                0x00
#define USB_DFU_STATE_APP_DETACH              0x01
#define USB_DFU_STATE_DFU_IDLE                0x02
#define USB_DFU_STATE_DFU_DNLOAD_SYNC         0x03
#define USB_DFU_STATE_DFU_DNBUSY              0x04
#define USB_DFU_STATE_DFU_DNLOAD_IDLE         0x05
#define USB_DFU_STATE_DFU_MANIFEST_SYNC       0x06
#define USB_DFU_STATE_DFU_MANIFEST            0x07
#define USB_DFU_STATE_DFU_MANIFEST_WAIT_RESET 0x08
#define USB_DFU_STATE_DFU_UPLOAD_IDLE         0x09
#define USB_DFU_STATE_DFU_ERROR               0x0A

__END_CDECLS

#endif  // SYSROOT_ZIRCON_HW_USB_DFU_H_
