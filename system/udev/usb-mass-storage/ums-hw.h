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
#define UMS_READ12                   0x8A
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

#define UMS_COMMAND_BLOCK_WRAPPER_SIZE              31
#define UMS_COMMAND_STATUS_WRAPPER_SIZE             13

// command lengths
#define UMS_INQUIRY_COMMAND_LENGTH                 6
#define UMS_TEST_UNIT_READY_COMMAND_LENGTH         9
#define UMS_REQUEST_SENSE_COMMAND_LENGTH           6
#define UMS_READ_FORMAT_CAPACITIES_COMMAND_LENGTH  10
#define UMS_READ_CAPACITY10_COMMAND_LENGTH         10
#define UMS_READ_CAPACITY16_COMMAND_LENGTH         16
#define UMS_READ10_COMMAND_LENGTH                  10
#define UMS_READ12_COMMAND_LENGTH                  12
#define UMS_READ16_COMMAND_LENGTH                  16
#define UMS_WRITE10_COMMAND_LENGTH                 10
#define UMS_WRITE12_COMMAND_LENGTH                 12
#define UMS_WRITE16_COMMAND_LENGTH                 16
#define UMS_TOGGLE_REMOVABLE_COMMAND_LENGTH        12

// transfer lengths
#define UMS_NO_TRANSFER_LENGTH                     0
#define UMS_INQUIRY_TRANSFER_LENGTH                0x24
#define UMS_REQUEST_SENSE_TRANSFER_LENGTH          0x12
#define UMS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH 0xFC
#define UMS_READ_CAPACITY10_TRANSFER_LENGTH        0x08
#define UMS_READ_CAPACITY16_TRANSFER_LENGTH        0x20