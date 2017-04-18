// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <driver/driver-api.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>

#include <mxio/util.h>
#include <mxio/remoteio.h>

#include "devcoordinator.h"
#include "devhost.h"

#define ios_from_ph(ph) containerof(ph, devhost_iostate_t, ph)

static mx_status_t dh_handle_rpc(port_handler_t* ph, mx_signals_t signals);

static port_t dh_port;

typedef struct devhost_iostate iostate_t;

static iostate_t root_ios = {
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


static const char* mkdevpath(mx_device_t* dev, char* path, size_t max) {
    if (dev == NULL) {
        return "";
    }
    if (max < 1) {
        return "<invalid>";
    }
    char* end = path + max;
    char sep = 0;

    while (dev) {
        *(--end) = sep;

        size_t len = strlen(dev->name);
        if (len > (size_t)(end - path)) {
            break;
        }
        end -= len;
        memcpy(end, dev->name, len);
        sep = '/';
        dev = dev->parent;
    }
    return end;
}

static mx_status_t dh_find_driver(const char* libname, driver_rec_t** out) {
    // check for already-loaded driver first
    driver_rec_t* rec;
    list_for_every_entry(&dh_drivers, rec, driver_rec_t, node) {
        if (!strcmp(libname, rec->libname)) {
            *out = rec;
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
                                  mx_handle_t h, iostate_t* ios) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t dh_handle_rpc_read(mx_handle_t h, iostate_t* ios) {
    dc_msg_t msg;
    mx_handle_t hin[2];
    uint32_t msize = sizeof(msg);
    uint32_t hcount = 2;

    mx_status_t r;
    if ((r = mx_channel_read(h, 0, &msg, msize, &msize,
                             hin, hcount, &hcount)) < 0) {
        return r;
    }

    char buffer[512];
    const char* path = mkdevpath(ios->dev, buffer, sizeof(buffer));

    // handle remoteio open messages only
    if ((msize >= MXRIO_HDR_SZ) && (MXRIO_OP(msg.op) == MXRIO_OPEN)) {
        if (hcount != 1) {
            r = ERR_INVALID_ARGS;
            goto fail;
        }
        printf("devhost[%s] remoteio OPEN\n", path);
        return dh_handle_open((void*) &msg, msize, hin[0], ios);
    }

    const void* data;
    const char* name;
    const char* args;
    if ((r = dc_msg_unpack(&msg, msize, &data, &name, &args)) < 0) {
        goto fail;
    }

    switch (msg.op) {
    case DC_OP_CREATE_DEVICE:
        // This does not operate under the devhost api lock,
        // since the newly created device is not visible to
        // any API surface until a driver is bound to it.
        // (which can only happen via another message on this thread)
        printf("devhost[%s] create device '%s'\n", path, name);
        if (msg.namelen > MX_DEVICE_NAME_MAX) {
            r = ERR_INVALID_ARGS;
            break;
        }
        if (hcount != 1) {
            r = ERR_INVALID_ARGS;
            break;
        }
        iostate_t* newios = calloc(1, sizeof(iostate_t));
        if (newios == NULL) {
            r = ERR_NO_MEMORY;
            break;
        }
        if ((newios->dev = calloc(1, sizeof(mx_device_t))) == NULL) {
            free(newios);
            r = ERR_NO_MEMORY;
            break;
        }
        mx_device_t* dev = newios->dev;
        memcpy(dev->name, name, msg.namelen + 1);
        dev->protocol_id = msg.protocol_id;
        dev->rpc = hin[0];
        dev->refcount = 1;
        list_initialize(&dev->children);
        //TODO: dev->ops and other lifecycle bits

        newios->ph.handle = hin[0];
        newios->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
        newios->ph.func = dh_handle_rpc;
        if ((r = port_watch(&dh_port, &newios->ph)) < 0) {
            free(newios);
            break;
        }
        printf("devhost[%s] created '%s' ios=%p\n", path, name, newios);
        return NO_ERROR;

    case DC_OP_BIND_DRIVER:
        //TODO: api lock integration
        printf("devhost[%s] bind driver '%s'\n", path, name);
        driver_rec_t* rec;
        if ((r = dh_find_driver(name, &rec)) < 0) {
            printf("devhost[%s] driver load failed: %d\n", path, r);
            //TODO: inform devcoord
        } else {
            if (rec->drv.ops.bind) {
                r = rec->drv.ops.bind(&rec->drv, ios->dev, &ios->dev->owner_cookie);
            } else {
                r = ERR_NOT_SUPPORTED;
            }
            if (r < 0) {
                printf("devhost[%s] bind driver '%s' failed: %d\n", path, name, r);
            }
            //TODO: inform devcoord
        }
        return NO_ERROR;

    default:
        printf("devhost[%s] invalid rpc op %08x\n", path, msg.op);
        r = ERR_NOT_SUPPORTED;
    }

fail:
    while (hcount > 0) {
        mx_handle_close(hin[--hcount]);
    }
    return r;
}

static mx_status_t dh_handle_rpc(port_handler_t* ph, mx_signals_t signals) {
    iostate_t* ios = ios_from_ph(ph);

    if (signals & MX_CHANNEL_READABLE) {
        mx_status_t r = dh_handle_rpc_read(ph->handle, ios);
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

// Send message to devcoordinator asking to add child device to
// parent device.  Called under devhost api lock.
mx_status_t devhost_add(mx_device_t* parent, mx_device_t* child) {
    char buffer[512];
    const char* path = mkdevpath(parent, buffer, sizeof(buffer));
    printf("devhost[%s] add '%s'\n", path, child->name);
    iostate_t* ios = calloc(1, sizeof(*ios));
    if (ios == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_handle_t h0, h1;
    mx_status_t r;
    if ((r = mx_channel_create(0, &h0, &h1)) < 0) {
        free(ios);
        return r;
    }

    dc_msg_t msg;
    dc_status_t rsp;
    mx_channel_call_args_t args = {
        .wr_bytes = &msg,
        .wr_handles = &h0,
        .rd_bytes = &rsp,
        .rd_handles = NULL,
        .wr_num_handles = 1,
        .rd_num_bytes = sizeof(rsp),
        .rd_num_handles = 0,
    };
    if ((r = dc_msg_pack(&msg, &args.wr_num_bytes,
                         NULL, 0, child->name, NULL)) < 0) {
fail_write:
        mx_handle_close(h0);
        mx_handle_close(h1);
        free(ios);
        return r;
    }
    msg.txid = 1;
    msg.op = DC_OP_ADD_DEVICE;
    msg.protocol_id = child->protocol_id;
    mx_status_t rdstatus;
    if ((r = mx_channel_call(parent->rpc, 0, MX_TIME_INFINITE,
                             &args, &args.rd_num_bytes, &args.rd_num_handles,
                             &rdstatus)) < 0) {
        printf("devhost: rpc:device_add write failed: %d\n", r);
        goto fail_write;
    }
    if (rdstatus < 0) {
        printf("devhost: rpc:device_add read failed: %d\n", rdstatus);
        r = rdstatus;
    } else if (args.rd_num_bytes != sizeof(rsp)) {
        printf("devhost: rpc:device_add bad response\n");
        r = ERR_INTERNAL;
    } else if ((r = rsp.status) < 0) {
        printf("devhost: rpc:device_add remote error: %d\n", r);
    } else {
        ios->dev = child;
        ios->ph.handle = h1;
        ios->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
        ios->ph.func = dh_handle_rpc;
        if ((r = port_watch(&dh_port, &ios->ph)) == NO_ERROR) {
            child->rpc = h1;
            return NO_ERROR;
        }
    }

    mx_handle_close(h1);
    free(ios);
    return r;
}

// Send message to devcoordinator informing it that this device
// is being removed.  Called under devhost api lock.
mx_status_t devhost_remove(mx_device_t* dev) {
    char buffer[512];
    const char* path = mkdevpath(dev, buffer, sizeof(buffer));
    printf("devhost[%s] remove\n", path);
    dc_msg_t msg;
    dc_status_t rsp;
    mx_channel_call_args_t args = {
        .wr_bytes = &msg,
        .wr_handles = NULL,
        .rd_bytes = &rsp,
        .rd_handles = NULL,
        .wr_num_handles = 0,
        .rd_num_bytes = sizeof(rsp),
        .rd_num_handles = 0,
    };
    mx_status_t r;
    if ((r = dc_msg_pack(&msg, &args.wr_num_bytes,
                         NULL, 0, NULL, NULL)) < 0) {
        return r;
    }
    msg.txid = 1;
    msg.op = DC_OP_REMOVE_DEVICE;
    msg.protocol_id = 0;
    mx_status_t rdstatus;
    if ((r = mx_channel_call(dev->rpc, 0, MX_TIME_INFINITE,
                             &args, &args.rd_num_bytes, &args.rd_num_handles,
                             &rdstatus)) < 0) {
        printf("devhost: rpc:device_remove write failed: %d\n", r);
        return r;
    }
    if (rdstatus < 0) {
        printf("devhost: rpc:device_remove read failed: %d\n", rdstatus);
        return rdstatus;
    }
    if (args.rd_num_bytes != sizeof(rsp)) {
        printf("devhost: rpc:device_remove bad response\n");
        return ERR_INTERNAL;
    }
    if (rsp.status < 0) {
        printf("devhost: rpc:device_remove remote error: %d\n", r);
        return rsp.status;
    }
    return NO_ERROR;
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

    root_ios.ph.handle = mx_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_USER0, 0));
    if (root_ios.ph.handle == MX_HANDLE_INVALID) {
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
    if ((r = port_watch(&dh_port, &root_ios.ph)) < 0) {
        printf("devhost: could not watch rpc channel: %d\n", r);
        return -1;
    }
    r = port_dispatch(&dh_port);
    printf("devhost: port dispatch finished: %d\n", r);

    return 0;
}