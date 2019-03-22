// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <fuchsia/hardware/pty/c/fidl.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define FIFOSIZE 256
#define FIFOMASK (FIFOSIZE - 1)

typedef struct console_ctx {
    zx_device_t* zxdev;
} console_device_t;

static struct {
    uint8_t data[FIFOSIZE];
    uint32_t head;
    uint32_t tail;
    mtx_t lock;
} fifo = {
    .lock = MTX_INIT,
};

static zx_status_t fifo_read(uint8_t* out) {
    if (fifo.head == fifo.tail) {
        return -1;
    }
    *out = fifo.data[fifo.tail];
    fifo.tail = (fifo.tail + 1) & FIFOMASK;
    return ZX_OK;
}

static void fifo_write(uint8_t x) {
    uint32_t next = (fifo.head + 1) & FIFOMASK;
    if (next != fifo.tail) {
        fifo.data[fifo.head] = x;
        fifo.head = next;
    }
}

static int debug_reader(void* arg) {
    zx_device_t* dev = arg;
    char ch;
    for (;;) {
        size_t length = 1;
        zx_status_t status = zx_debug_read(get_root_resource(), &ch, &length);
        if (status == ZX_OK && length == 1) {
            mtx_lock(&fifo.lock);
            if (fifo.head == fifo.tail) {
                device_state_set(dev, DEV_STATE_READABLE);
            }
            fifo_write(ch);
            mtx_unlock(&fifo.lock);
        } else if (status == ZX_ERR_NOT_SUPPORTED) {
            // Silently exit
            return 0;
        } else {
            printf("console: error %d, length %zu from zx_debug_read syscall, exiting.\n",
                    status, length);

            return status;
        }
    }
    return 0;
}

static zx_status_t console_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
    console_device_t* console = ctx;

    uint8_t* data = buf;
    mtx_lock(&fifo.lock);
    while (count-- > 0) {
        if (fifo_read(data))
            break;
        data++;
    }
    if (fifo.head == fifo.tail) {
        device_state_clr(console->zxdev, DEV_STATE_READABLE);
    }
    mtx_unlock(&fifo.lock);
    ssize_t length = data - (uint8_t*)buf;
    if (length == 0) {
        return ZX_ERR_SHOULD_WAIT;
    }
    *actual = length;
    return ZX_OK;
}

#define MAX_WRITE_SIZE 256

static zx_status_t console_write(void* ctx, const void* buf, size_t count, zx_off_t off, size_t* actual) {
    const void* ptr = buf;
    zx_status_t status = ZX_OK;
    size_t total = 0;
    while (count > 0) {
        size_t xfer = (count > MAX_WRITE_SIZE) ? MAX_WRITE_SIZE : count;
        if ((status = zx_debug_write(ptr, xfer)) < 0) {
            break;
        }
        ptr += xfer;
        count -= xfer;
        total += xfer;
    }
    if (total > 0) {
        *actual = total;
        status = ZX_OK;
     }
     return status;
}

static void console_release(void* ctx) {
    console_device_t* console = ctx;
    free(console);
}

static zx_status_t console_OpenClient(void* ctx, uint32_t id, zx_handle_t handle,
                                      fidl_txn_t* txn) {
    return fuchsia_hardware_pty_DeviceOpenClient_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t console_ClrSetFeature(void* ctx, uint32_t clr, uint32_t set, fidl_txn_t* txn) {
    return fuchsia_hardware_pty_DeviceClrSetFeature_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
}

static zx_status_t console_GetWindowSize(void* ctx, fidl_txn_t* txn) {
    fuchsia_hardware_pty_WindowSize wsz = {
        .width = 0,
        .height = 0
    };
    return fuchsia_hardware_pty_DeviceGetWindowSize_reply(txn, ZX_ERR_NOT_SUPPORTED, &wsz);
}

static zx_status_t console_MakeActive(void* ctx, uint32_t client_pty_id, fidl_txn_t* txn) {
    return fuchsia_hardware_pty_DeviceMakeActive_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t console_ReadEvents(void* ctx, fidl_txn_t* txn) {
    return fuchsia_hardware_pty_DeviceReadEvents_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
}

static zx_status_t console_SetWindowSize(void* ctx, const fuchsia_hardware_pty_WindowSize* size,
                                         fidl_txn_t* txn) {
    return fuchsia_hardware_pty_DeviceSetWindowSize_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static fuchsia_hardware_pty_Device_ops_t fidl_ops = {
    .OpenClient = console_OpenClient,
    .ClrSetFeature = console_ClrSetFeature,
    .GetWindowSize = console_GetWindowSize,
    .MakeActive = console_MakeActive,
    .ReadEvents = console_ReadEvents,
    .SetWindowSize = console_SetWindowSize
};

static zx_status_t console_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_pty_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

static zx_protocol_device_t console_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = console_read,
    .write = console_write,
    .release = console_release,
    .message = console_message
};

static zx_status_t console_bind(void* ctx, zx_device_t* parent) {
    // If we're in an isolated devmgr, we won't have the root resource.  In that
    // case, just don't bind this driver.
    if (get_root_resource() == ZX_HANDLE_INVALID) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    console_device_t* console = calloc(1, sizeof(console_device_t));
    if (!console) {
        return ZX_ERR_NO_MEMORY;
    }
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "console",
        .ctx = console,
        .ops = &console_device_proto,
    };

    zx_status_t status = device_add(parent, &args, &console->zxdev);
    if (status != ZX_OK) {
        printf("console: device_add() failed\n");
        free(console);
        return status;
    }

    thrd_t t;
    thrd_create_with_name(&t, debug_reader, console->zxdev, "debug-reader");

    return ZX_OK;
}

static zx_driver_ops_t console_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = console_bind,
};

ZIRCON_DRIVER_BEGIN(console, console_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(console)
