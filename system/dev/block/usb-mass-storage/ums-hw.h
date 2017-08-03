// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

// SCSI commands
#define UMS_TEST_UNIT_READY          0x00
#define UMS_REQUEST_SENSE            0x03
#define UMS_INQUIRY                  0x12
#define UMS_MODE_SELECT6             0x15
#define UMS_MODE_SENSE6              0x1A
#define UMS_START_STOP_UNIT          0x1B
#define UMS_TOGGLE_REMOVABLE         0x1E
#define UMS_READ_FORMAT_CAPACITIES   0x23
#define UMS_READ_CAPACITY10          0x25
#define UMS_READ10                   0x28
#define UMS_WRITE10                  0x2A
#define UMS_SYNCHRONIZE_CACHE        0x35
#define UMS_MODE_SELECT10            0x55
#define UMS_MODE_SENSE10             0x5A
#define UMS_READ16                   0x88
#define UMS_WRITE16                  0x8A
#define UMS_READ_CAPACITY16          0x9E
#define UMS_READ12                   0xA8
#define UMS_WRITE12                  0xAA

// control request values
#define USB_REQ_RESET               0xFF
#define USB_REQ_GET_MAX_LUN         0xFE

// fs = feature selector, don't really know much about control requests,
// so not sure what that means
#define FS_ENDPOINT_HALT            0x00

// error codes for CSW processing
typedef enum {CSW_SUCCESS, CSW_FAILED, CSW_PHASE_ERROR, CSW_INVALID,
                CSW_TAG_MISMATCH} csw_status_t;

// signatures in header and status
#define CBW_SIGNATURE               0x43425355
#define CSW_SIGNATURE               0x53425355

// transfer lengths
#define UMS_INQUIRY_TRANSFER_LENGTH                0x24
#define UMS_REQUEST_SENSE_TRANSFER_LENGTH          0x12
#define UMS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH 0xFC

// 6 Byte SCSI command
// This is big endian
typedef struct {
    uint8_t     opcode;
    uint8_t     misc;
    uint16_t    lba;    // logical block address
    uint8_t     length;
    uint8_t     control;
} __PACKED scsi_command6_t;
static_assert(sizeof(scsi_command6_t) == 6, "");

// 10 Byte SCSI command
// This is big endian
typedef struct {
    uint8_t     opcode;
    uint8_t     misc;
    uint32_t    lba;    // logical block address
    uint8_t     misc2;
    uint8_t     length_hi; // break length into two pieces to avoid odd alignment
    uint8_t     length_lo;
    uint8_t     control;
} __PACKED scsi_command10_t;
static_assert(sizeof(scsi_command10_t) == 10, "");

// 12 Byte SCSI command
// This is big endian
typedef struct {
    uint8_t     opcode;
    uint8_t     misc;
    uint32_t    lba;    // logical block address
    uint32_t    length;
    uint8_t     misc2;
    uint8_t     control;
} __PACKED scsi_command12_t;
static_assert(sizeof(scsi_command12_t) == 12, "");

// 16 Byte SCSI command
// This is big endian
typedef struct {
    uint8_t     opcode;
    uint8_t     misc;
    uint64_t    lba;    // logical block address
    uint32_t    length;
    uint8_t     misc2;
    uint8_t     control;
} __PACKED scsi_command16_t;
static_assert(sizeof(scsi_command16_t) == 16, "");

// SCSI Read Capacity 10 payload
// This is big endian
typedef struct {
    uint32_t    lba;
    uint32_t    block_length;
} __PACKED scsi_read_capacity_10_t;
static_assert(sizeof(scsi_read_capacity_10_t) == 8, "");

// SCSI Read Capacity 16 payload
// This is big endian
typedef struct {
    uint64_t    lba;
    uint32_t    block_length;
    uint8_t     ptype_prot_en;  // bit 0: PROT_EN, bits 1-3: P_TYPE
    uint8_t     resesrved[19];
} __PACKED scsi_read_capacity_16_t;
static_assert(sizeof(scsi_read_capacity_16_t) == 32, "");

// SCSI Mode Sense 6 command
typedef struct {
    uint8_t     opcode; // UMS_MODE_SENSE6
    uint8_t     disable_block_desc;
    uint8_t     page;
    uint8_t     subpage;
    uint8_t     allocation_length;
    uint8_t     control;
} __PACKED scsi_mode_sense_6_command_t;
static_assert(sizeof(scsi_mode_sense_6_command_t) == 6, "");

// SCSI Mode Sense 6 data response
typedef struct {
    uint8_t     mode_data_length;
    uint8_t     medium_type;
    uint8_t     device_specific_param;
    uint8_t     block_desc_length;
} __PACKED scsi_mode_sense_6_data_t;
#define MODE_SENSE_DSP_RO   0x80    //  bit 7 of device_specific_param: read-only

// Command Block Wrapper
typedef struct {
    uint32_t    dCBWSignature;      // CBW_SIGNATURE
    uint32_t    dCBWTag;
    uint32_t    dCBWDataTransferLength;
    uint8_t     bmCBWFlags;
    uint8_t     bCBWLUN;
    uint8_t     bCBWCBLength;
    uint8_t     CBWCB[16];
} __PACKED ums_cbw_t;
static_assert(sizeof(ums_cbw_t) == 31, "");

// Command Status Wrapper
typedef struct {
    uint32_t    dCSWSignature;      // CSW_SIGNATURE
    uint32_t    dCSWTag;
    uint32_t    dCSWDataResidue;
    uint8_t     bmCSWStatus;
} __PACKED ums_csw_t;
static_assert(sizeof(ums_csw_t) == 13, "");
