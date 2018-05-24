// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/rawnand.h>
#include <limits.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>
#include <zircon/threads.h>
#include <zircon/types.h>

typedef struct nand_device {
    zx_device_t* zxdev;
    nand_protocol_t nand_proto;
    raw_nand_protocol_t host;

    nand_info_t nand_info;
    uint32_t num_nand_pages;

    // Protects the IO request list.
    mtx_t lock;
    list_node_t io_list;

    thrd_t worker_thread;
    zx_handle_t worker_event;
} nand_device_t;

// Nand io transactions. One per client request.
typedef struct nand_io {
    nand_op_t nand_op;
    list_node_t node;
} nand_io_t;

zx_status_t nand_read_page(nand_device_t* dev, void* data, void* oob, uint32_t nand_page,
                           int* ecc_correct, int retries);
zx_status_t nand_write_page(nand_device_t* dev, void* data, void* oob, uint32_t nand_page);
zx_status_t nand_erase_block(nand_device_t* dev, uint32_t nand_page);

// The following is only used for testing purposes.
typedef struct nand_test_cmd_read_pages {
    uint32_t num_pages;
    uint32_t nand_page;
} nandtest_cmd_read_pages_t;

// ioctl to read/write a single page + oob.
// Since this is test-only, vmo offset must be 0 for both vmo's.
typedef struct nand_test_rw_page_data_oob {
    // The vmo's must be at the beginning !
    // The ioctl code will dup handles for these
    // in the callee's descriptor space. And the
    // ioctl code looks for the vmo's at the start
    // of this struct.
    zx_handle_t data; // Data vmo.
    zx_handle_t oob;  // Oob vmo.
    uint32_t nand_page;
    uint32_t data_len; // In NAND pages, must be 1.
    uint32_t oob_len;
} nandtest_rw_page_data_oob_t;

typedef struct nand_test_cmd_erase_block {
    uint32_t nandblock;
} nandtest_cmd_erase_block_t;

// Responses from read/write/erase ioctls.
typedef struct nand_test_resp {
    zx_status_t status;
} nandtest_resp_t;

#define IOCTL_NAND_ERASE_BLOCK \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NAND_TEST, 1)
#define IOCTL_NAND_GET_NAND_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NAND_TEST, 2)
#define IOCTL_NAND_READ_PAGE_DATA_OOB \
    IOCTL(IOCTL_KIND_SET_TWO_HANDLES, IOCTL_FAMILY_NAND_TEST, 3)
#define IOCTL_NAND_WRITE_PAGE_DATA_OOB \
    IOCTL(IOCTL_KIND_SET_TWO_HANDLES, IOCTL_FAMILY_NAND_TEST, 4)

IOCTL_WRAPPER_INOUT(ioctl_nand_erase_block, IOCTL_NAND_ERASE_BLOCK, nandtest_cmd_erase_block_t,
                    nandtest_resp_t);
IOCTL_WRAPPER_INOUT(ioctl_nand_write_page_data_oob, IOCTL_NAND_WRITE_PAGE_DATA_OOB,
                    nandtest_rw_page_data_oob_t, nandtest_resp_t);
IOCTL_WRAPPER_IN_VAROUT(ioctl_nand_read_page_data_oob, IOCTL_NAND_READ_PAGE_DATA_OOB,
                        nandtest_rw_page_data_oob_t, void);
IOCTL_WRAPPER_OUT(ioctl_nand_get_nand_info, IOCTL_NAND_GET_NAND_INFO, void);
