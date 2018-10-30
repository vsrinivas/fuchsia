// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/nand.h>
#include <ddk/protocol/rawnand.h>
#include <zircon/threads.h>
#include <zircon/types.h>

typedef struct nand_device {
    zx_device_t* zxdev;
    nand_protocol_t nand_proto;
    raw_nand_protocol_t host;

    zircon_nand_Info nand_info;
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
                           uint32_t* ecc_correct, int retries);
zx_status_t nand_write_page(nand_device_t* dev, void* data, void* oob, uint32_t nand_page);
zx_status_t nand_erase_block(nand_device_t* dev, uint32_t nand_page);
