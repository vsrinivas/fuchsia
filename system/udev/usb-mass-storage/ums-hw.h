// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

// SCSI commands
#define MS_TEST_UNIT_READY          0x00
#define MS_REQUEST_SENSE            0x03
#define MS_INQUIRY                  0x12
#define MS_MODE_SELECT6             0x15
#define MS_MODE_SENSE6              0x1A
#define MS_START_STOP_UNIT          0x1B
#define MS_TOGGLE_REMOVABLE         0x1E
#define MS_READ_FORMAT_CAPACITIES   0x23
#define MS_READ_CAPACITY10          0x25
#define MS_READ10                   0x28
#define MS_WRITE10                  0x2A
#define MS_SYNCHRONIZE_CACHE        0x35
#define MS_MODE_SELECT10            0x55
#define MS_MODE_SENSE10             0x5A
#define MS_READ16                   0x88
#define MS_WRITE16                  0x8A
#define MS_READ_CAPACITY16          0x9E
#define MS_READ12                   0x8A
#define MS_WRITE12                  0xAA

// control request values
#define USB_REQ_RESET               0xFF
#define USB_REQ_GET_MAX_LUN         0xFE

// fs = feature selector, don't really know much about control requests,
// so not sure what that means
#define FS_ENDPOINT_HALT            0x00

// read flag enums
#define USE_READ10                  1
#define USE_READ12                  2
#define USE_READ16                  3

// error codes for CSW processing
typedef enum {CSW_SUCCESS, CSW_FAILED, CSW_PHASE_ERROR, CSW_INVALID,
                CSW_TAG_MISMATCH} csw_status_t;

// signatures in header and status
#define CBW_SIGNATURE               0x43425355
#define CSW_SIGNATURE               0x53425355

// command lengths
#define MS_INQUIRY_COMMAND_LENGTH                 6
#define MS_TEST_UNIT_READY_COMMAND_LENGTH         9
#define MS_REQUEST_SENSE_COMMAND_LENGTH           6
#define MS_READ_FORMAT_CAPACITIES_COMMAND_LENGTH  10
#define MS_READ_CAPACITY10_COMMAND_LENGTH         10
#define MS_READ_CAPACITY16_COMMAND_LENGTH         10
#define MS_READ10_COMMAND_LENGTH                  10
#define MS_READ12_COMMAND_LENGTH                  12
#define MS_READ16_COMMAND_LENGTH                  16
#define MS_WRITE10_COMMAND_LENGTH                 10
#define MS_WRITE12_COMMAND_LENGTH                 12
#define MS_WRITE16_COMMAND_LENGTH                 16
#define MS_TOGGLE_REMOVABLE_COMMAND_LENGTH        12

// transfer lengths
#define MS_NO_TRANSFER_LENGTH                     0
#define MS_INQUIRY_TRANSFER_LENGTH                0x24
#define MS_REQUEST_SENSE_TRANSFER_LENGTH          0x12
#define MS_READ_FORMAT_CAPACITIES_TRANSFER_LENGTH 0xFC
#define MS_READ_CAPACITY10_TRANSFER_LENGTH        0x08
#define MS_READ_CAPACITY16_TRANSFER_LENGTH        0x20