// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/nand/c/fidl.h>

#define IOCTL_NAND_BROKER_UNLINK \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NAND_BROKER, 1)

#define IOCTL_NAND_BROKER_GET_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NAND_BROKER, 2)

#define IOCTL_NAND_BROKER_READ \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_NAND_BROKER, 3)

#define IOCTL_NAND_BROKER_WRITE \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_NAND_BROKER, 4)

#define IOCTL_NAND_BROKER_ERASE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NAND_BROKER, 5)

// Defines a Read/Write/Erase request to be sent to the nand driver.
typedef struct nand_broker_request {
    zx_handle_t vmo;            // Only used for read and write.
    uint32_t length;            // In pages (read / write) or blocks (erase).
    uint32_t offset_nand;       // In pages (read / write) or blocks (erase).
    uint64_t offset_data_vmo;   // In pages.
    uint64_t offset_oob_vmo;    // In pages.
    bool data_vmo;              // True to read or write data.
    bool oob_vmo;               // True to read or write OOB data.
} nand_broker_request_t;

// Driver's response for a Read/Write/Erase request.
typedef struct nand_broker_response {
    zx_status_t status;
    uint32_t    corrected_bit_flips;  // Only used for read.
} nand_broker_response_t;

// ssize_t ioctl_nand_broker_unlink(int fd);
IOCTL_WRAPPER(ioctl_nand_broker_unlink, IOCTL_NAND_BROKER_UNLINK);

// ssize_t ioctl_nand_broker_get_info(int fd, zircon_nand_Info* nand_info);
IOCTL_WRAPPER_OUT(ioctl_nand_broker_get_info, IOCTL_NAND_BROKER_GET_INFO, zircon_nand_Info);

// ssize_t ioctl_nand_broker_read(int fd, nand_broker_request_t* request,
//                                nand_broker_response_t* response);
IOCTL_WRAPPER_INOUT(ioctl_nand_broker_read, IOCTL_NAND_BROKER_READ,
                    nand_broker_request_t, nand_broker_response_t);

// ssize_t ioctl_nand_broker_write(int fd, nand_broker_request_t* request,
//                                 nand_broker_response_t* response);
IOCTL_WRAPPER_INOUT(ioctl_nand_broker_write, IOCTL_NAND_BROKER_WRITE,
                    nand_broker_request_t, nand_broker_response_t);

// ssize_t ioctl_nand_broker_erase(int fd, uint32_t* bad_block_entries,
//                                 nand_broker_response_t* response);
IOCTL_WRAPPER_INOUT(ioctl_nand_broker_erase, IOCTL_NAND_BROKER_ERASE,
                    nand_broker_request_t, nand_broker_response_t);
