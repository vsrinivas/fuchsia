// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pave-utils.h"

#include <stdio.h>

#include <block-client/cpp/client.h>
#include <fbl/unique_fd.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/fifo.h>
#include <zircon/status.h>

#include <utility>

#include "pave-logging.h"

zx_status_t FlushClient(const block_client::Client& client) {
    block_fifo_request_t request;
    request.group = 0;
    request.vmoid = VMOID_INVALID;
    request.opcode = BLOCKIO_FLUSH;
    request.length = 0;
    request.vmo_offset = 0;
    request.dev_offset = 0;

    zx_status_t status = client.Transaction(&request, 1);
    if (status != ZX_OK) {
        ERROR("Error flushing: %s\n", zx_status_get_string(status));
        return status;
    }
    return ZX_OK;
}

zx_status_t FlushBlockDevice(const fbl::unique_fd& fd) {
    zx::fifo fifo;
    fzl::UnownedFdioCaller caller(fd.get());
    zx_status_t status;
    zx_status_t io_status = fuchsia_hardware_block_BlockGetFifo(caller.borrow_channel(), &status,
                                                                fifo.reset_and_get_address());
    if (io_status != ZX_OK) {
        status = io_status;
    }
    if (status != ZX_OK) {
        ERROR("Couldn't attach fifo to partition\n");
        return status;
    }

    block_client::Client client;
    status = block_client::Client::Create(std::move(fifo), &client);
    if (status != ZX_OK) {
        ERROR("Couldn't create block client\n");
        return status;
    }

    return FlushClient(client);
}
