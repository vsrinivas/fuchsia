// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <driver/driver-api.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>

#include <mxio/util.h>
#include <mxio/remoteio.h>

#include "devcoordinator.h"

typedef struct {
    port_handler_t ph;
    mx_device_t* dev;
} device_ctx_t;

#define ctx_from_ph(ph) containerof(ph, device_ctx_t, ph)

static mx_status_t dh_handle_rpc(port_handler_t* ph, mx_signals_t signals);

static port_t dh_port;

static device_ctx_t devhost_root_ctx = {
    .ph = {
        .waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
        .func = dh_handle_rpc,
    },
};

typedef struct {
    mx_driver_t drv;
    list_node_t node;
    mx_status_t status;
    const char* libname;
} driver_rec_t;

static list_node_t dh_drivers = LIST_INITIAL_VALUE(dh_drivers);


static mx_status_t dh_find_driver(const char* libname, driver_rec_t** out) {
    // check for already-loaded driver first
    driver_rec_t* rec;
    list_for_every_entry(&dh_drivers, rec, driver_rec_t, node) {
        if (!strcmp(libname, rec->libname)) {
            return rec->status;
        }
    }

    int len = strlen(libname) + 1;
    rec = calloc(1, sizeof(driver_rec_t) + len);
    if (rec == NULL) {
        return ERR_NO_MEMORY;
    }
    memcpy((void*) (rec + 1), libname, len);
    rec->libname = (const char*) (rec + 1);
    list_add_tail(&dh_drivers, &rec->node);
    *out = rec;

    void* dl = dlopen(libname, RTLD_NOW);
    if (dl == NULL) {
        printf("devhost: cannot load '%s': %s\n", libname, dlerror());
        rec->status = ERR_IO;
        goto done;
    }

    magenta_driver_info_t* di = dlsym(dl, "__magenta_driver__");
    if (di == NULL) {
        printf("devhost: driver '%s' missing __magenta_driver__ symbol\n", libname);
        rec->status = ERR_IO;
        goto done;
    }

    printf("devhost: loaded '%s'\n", libname);
    memcpy(&rec->drv.ops, &di->driver->ops, sizeof(mx_driver_ops_t));
    rec->drv.flags = di->driver->flags;

    if (rec->drv.ops.init) {
        rec->status = rec->drv.ops.init(&rec->drv);
        if (rec->status < 0) {
            printf("devhost: driver '%s' failed in init: %d\n",
                   libname, rec->status);
        }
    } else {
        rec->status = NO_ERROR;
    }

done:
    return rec->status;
}

static mx_status_t dh_handle_open(mxrio_msg_t* msg, size_t len,
                                  mx_handle_t h, device_ctx_t* ctx) {
    printf("devhost: remoteio open\n");
    return ERR_NOT_SUPPORTED;
}

static mx_status_t dh_handle_rpc_read(mx_handle_t h, device_ctx_t* ctx) {
    dc_msg_t msg;
    mx_handle_t hin[2];
    uint32_t msize = sizeof(msg);
    uint32_t hcount = 2;

    mx_status_t r;
    if ((r = mx_channel_read(h, 0, &msg, msize, &msize,
                             hin, hcount, &hcount)) < 0) {
        return r;
    }

    // handle remoteio open messages only
    if ((msize >= MXRIO_HDR_SZ) && (msg.op == MXRIO_OPEN)) {
        if (hcount != 1) {
            r = ERR_INVALID_ARGS;
            goto fail;
        }
        return dh_handle_open((void*) &msg, msize, hin[0], ctx);
    }

    const void* data;
    const char* name;
    const char* args;
    if ((r = dc_msg_unpack(&msg, msize, &data, &name, &args)) < 0) {
        goto fail;
    }

    switch (msg.op) {
    case DC_OP_CREATE_DEVICE:
        printf("devhost: create device '%s'\n", name);
        if (hcount != 1) {
            r = ERR_INVALID_ARGS;
            break;
        }
        device_ctx_t* newctx = calloc(1, sizeof(device_ctx_t));
        if (newctx == NULL) {
            r = ERR_NO_MEMORY;
            break;
        }
        newctx->ph.handle = hin[0];
        newctx->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
        newctx->ph.func = dh_handle_rpc;
        if ((r = port_watch(&dh_port, &newctx->ph)) < 0) {
            free(newctx);
            break;
        }
        printf("devhost: (%p) device '%s' ctx=%p\n", ctx, name, newctx);
        return NO_ERROR;

    case DC_OP_BIND_DRIVER:
        printf("devhost: (%p) bind driver '%s'\n", ctx, name);
        driver_rec_t* rec;
        if ((r = dh_find_driver(name, &rec)) < 0) {
            printf("devhost: (%p) driver load failed: %d\n", ctx, r);
        }
        return NO_ERROR;

    default:
        printf("devhost: (%p) invalid rpc op %08x\n", ctx, msg.op);
        r = ERR_NOT_SUPPORTED;
    }

fail:
    while (hcount > 0) {
        mx_handle_close(hin[--hcount]);
    }
    return r;
}

static mx_status_t dh_handle_rpc(port_handler_t* ph, mx_signals_t signals) {
    device_ctx_t* ctx = ctx_from_ph(ph);

    if (signals & MX_CHANNEL_READABLE) {
        mx_status_t r = dh_handle_rpc_read(ph->handle, ctx);
        if (r != NO_ERROR) {
            printf("devhost: devmgr rpc unhandleable %p\n", ph);
            exit(0);
        }
        return r;
    }
    if (signals & MX_CHANNEL_PEER_CLOSED) {
        printf("devhost: devmgr disconnected!\n");
        exit(0);
    }
    printf("devhost: no work? %08x\n", signals);
    return NO_ERROR;
}


static void devhost_io_init(void) {
    mx_handle_t h;
    if (mx_log_create(MX_LOG_FLAG_DEVICE, &h) < 0) {
        return;
    }
    mxio_t* logger;
    if ((logger = mxio_logger_create(h)) == NULL) {
        return;
    }
    close(1);
    mxio_bind_to_fd(logger, 1, 0);
    dup2(1, 2);
}


mx_status_t devhost_add(mx_device_t* parent, mx_device_t* child) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t devhost_remove(mx_device_t* dev) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t devhost_device_rebind(mx_device_t* dev) {
    return ERR_NOT_SUPPORTED;
}

extern driver_api_t devhost_api;

mx_handle_t root_resource_handle;



int main(int argc, char** argv) {
    devhost_io_init();

    printf("devhost: main()\n");

    driver_api_init(&devhost_api);

    devhost_root_ctx.ph.handle = mx_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_USER0, 0));
    if (devhost_root_ctx.ph.handle == MX_HANDLE_INVALID) {
        printf("devhost: rpc handle invalid\n");
        return -1;
    }

    root_resource_handle = mx_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_RESOURCE, 0));
    if (root_resource_handle == MX_HANDLE_INVALID) {
        printf("devhost: no root resource handle!\n");
    }

    mx_status_t r;
    if ((r = port_init(&dh_port)) < 0) {
        printf("devhost: could not create port: %d\n", r);
        return -1;
    }
    if ((r = port_watch(&dh_port, &devhost_root_ctx.ph)) < 0) {
        printf("devhost: could not watch rpc channel: %d\n", r);
        return -1;
    }
    r = port_dispatch(&dh_port);
    printf("devhost: port dispatch finished: %d\n", r);

    return 0;
}