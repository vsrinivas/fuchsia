// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <inttypes.h>
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
#include "log.h"

uint32_t log_flags = LOG_ERROR | LOG_INFO;


#define ios_from_ph(ph) containerof(ph, devhost_iostate_t, ph)

static mx_status_t dh_handle_dc_rpc(port_handler_t* ph, mx_signals_t signals);

static port_t dh_port;

typedef struct devhost_iostate iostate_t;

static iostate_t root_ios = {
    .ph = {
        .waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
        .func = dh_handle_dc_rpc,
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
        log(ERROR, "devhost: cannot load '%s': %s\n", libname, dlerror());
        rec->status = ERR_IO;
        goto done;
    }

    magenta_driver_info_t* di = dlsym(dl, "__magenta_driver__");
    if (di == NULL) {
        log(ERROR, "devhost: driver '%s' missing __magenta_driver__ symbol\n", libname);
        rec->status = ERR_IO;
        goto done;
    }
    if (!di->driver->ops) {
        log(ERROR, "devhost: driver '%s' has NULL ops\n", libname);
        rec->status = ERR_INVALID_ARGS;
        goto done;
    }
    if (di->driver->ops->version != DRIVER_OPS_VERSION) {
        log(ERROR, "devhost: driver '%s' has bad driver ops version %" PRIx64
            ", expecting %" PRIx64 "\n", libname,
            di->driver->ops->version, DRIVER_OPS_VERSION);
        rec->status = ERR_INVALID_ARGS;
        goto done;
    }

    rec->drv.ops = di->driver->ops;
    rec->drv.flags = di->driver->flags;

    if (rec->drv.ops->init) {
        rec->status = rec->drv.ops->init(&rec->drv);
        if (rec->status < 0) {
            log(ERROR, "devhost: driver '%s' failed in init: %d\n",
                libname, rec->status);
        }
    } else {
        rec->status = NO_ERROR;
    }

done:
    return rec->status;
}

static void dh_handle_open(mxrio_msg_t* msg, size_t len,
                           mx_handle_t h, iostate_t* ios) {
    if ((msg->hcount != 1) ||
        (msg->datalen != (len - MXRIO_HDR_SZ))) {
        mx_handle_close(h);
        log(ERROR, "devhost: malformed OPEN reques\n");
        return;
    }
    msg->handle[0] = h;

    bool free_ios = false;
    mx_status_t r;
    if ((r = _devhost_rio_handler(msg, 0, ios, &free_ios)) < 0) {
        if (r != ERR_DISPATCHER_INDIRECT) {
            log(ERROR, "devhost: OPEN failed: %d\n", r);
        }
    }
}

static mx_status_t dh_handle_rpc_read(mx_handle_t h, iostate_t* ios) {
    dc_msg_t msg;
    mx_handle_t hin[2];
    uint32_t msize = sizeof(msg);
    uint32_t hcount = 2;

    mx_status_t r;
    if ((r = mx_channel_read(h, 0, &msg, hin, msize,
                             hcount, &msize, &hcount)) < 0) {
        return r;
    }

    char buffer[512];
    const char* path = mkdevpath(ios->dev, buffer, sizeof(buffer));

    // handle remoteio open messages only
    if ((msize >= MXRIO_HDR_SZ) && (MXRIO_OP(msg.op) == MXRIO_OPEN)) {
        if (hcount != 1) {
            goto fail;
        }
        log(RPC_RIO, "devhost[%s] remoteio OPEN\n", path);
        dh_handle_open((void*) &msg, msize, hin[0], ios);
        return NO_ERROR;
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
        log(RPC_IN, "devhost[%s] create device drv='%s'\n", path, name);
        if (hcount == 1) {
            // no optional resource handle
            hin[1] = MX_HANDLE_INVALID;
        } else if (hcount != 2) {
            r = ERR_INVALID_ARGS;
            break;
        }
        iostate_t* newios = calloc(1, sizeof(iostate_t));
        if (newios == NULL) {
            r = ERR_NO_MEMORY;
            break;
        }

        //TODO: dev->ops and other lifecycle bits
        if (name[0] == 0) {
            // no name means a dummy shadow device
            if ((newios->dev = calloc(1, sizeof(mx_device_t))) == NULL) {
                free(newios);
                r = ERR_NO_MEMORY;
                break;
            }
            mx_device_t* dev = newios->dev;
            memcpy(dev->name, "shadow", 7);
            dev->protocol_id = msg.protocol_id;
            dev->rpc = hin[0];
            dev->refcount = 1;
            list_initialize(&dev->children);
        } else {
            // named driver -- ask it to create the device
            driver_rec_t* rec;
            if ((r = dh_find_driver(name, &rec)) < 0) {
                log(ERROR, "devhost[%s] driver load failed: %d\n", path, r);
            } else {
                if (rec->drv.ops->create) {
                    r = rec->drv.ops->create(&rec->drv, "shadow", args, hin[1],
                                            &newios->dev);
                } else {
                    r = ERR_NOT_SUPPORTED;
                }
                if (r == NO_ERROR) {
                    r = devhost_device_install(newios->dev);
                }
                if (r < 0) {
                    log(ERROR, "devhost[%s] create (by '%s') failed: %d\n",
                        path, name, r);
                } else {
                    newios->dev->rpc = hin[0];
                }
            }
            //TODO: inform devcoord
        }

        newios->ph.handle = hin[0];
        newios->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
        newios->ph.func = dh_handle_dc_rpc;
        if ((r = port_watch(&dh_port, &newios->ph)) < 0) {
            free(newios);
            break;
        }
        log(RPC_IN, "devhost[%s] created '%s' ios=%p\n", path, name, newios);
        return NO_ERROR;

    case DC_OP_BIND_DRIVER:
        //TODO: api lock integration
        log(RPC_IN, "devhost[%s] bind driver '%s'\n", path, name);
        driver_rec_t* rec;
        if ((r = dh_find_driver(name, &rec)) < 0) {
            log(ERROR, "devhost[%s] driver load failed: %d\n", path, r);
        } else {
            if (rec->drv.ops->bind) {
                r = rec->drv.ops->bind(&rec->drv, ios->dev, &ios->dev->owner_cookie);
            } else {
                r = ERR_NOT_SUPPORTED;
            }
            if (r < 0) {
                log(ERROR, "devhost[%s] bind driver '%s' failed: %d\n", path, name, r);
            }
        }
        dc_msg_t reply = {
            .txid = 0,
            .op = DC_OP_STATUS,
            .status = r,
        };
        mx_channel_write(h, 0, &reply, sizeof(reply), NULL, 0);
        return NO_ERROR;

    default:
        log(ERROR, "devhost[%s] invalid rpc op %08x\n", path, msg.op);
        r = ERR_NOT_SUPPORTED;
    }

fail:
    while (hcount > 0) {
        mx_handle_close(hin[--hcount]);
    }
    return r;
}

// handles devcoordinator rpc
static mx_status_t dh_handle_dc_rpc(port_handler_t* ph, mx_signals_t signals) {
    iostate_t* ios = ios_from_ph(ph);

    if (signals & MX_CHANNEL_READABLE) {
        mx_status_t r = dh_handle_rpc_read(ph->handle, ios);
        if (r != NO_ERROR) {
            log(ERROR, "devhost: devmgr rpc unhandleable %p. fatal.\n", ph);
            exit(0);
        }
        return r;
    }
    if (signals & MX_CHANNEL_PEER_CLOSED) {
        log(ERROR, "devhost: devmgr disconnected! fatal.\n");
        exit(0);
    }
    log(ERROR, "devhost: no work? %08x\n", signals);
    return NO_ERROR;
}


static mx_status_t rio_handler(mxrio_msg_t* msg, mx_handle_t h, void* cookie) {
    iostate_t* ios = cookie;
    bool free_ios = false;
    mx_status_t r = _devhost_rio_handler(msg, 0, ios, &free_ios);
    return r;
};

// handles remoteio rpc
static mx_status_t dh_handle_rio_rpc(port_handler_t* ph, mx_signals_t signals) {
    iostate_t* ios = ios_from_ph(ph);

    mx_status_t r;
    const char* msg;
    if (signals & MX_CHANNEL_READABLE) {
        if ((r = mxrio_handle_rpc(ph->handle, rio_handler, ios)) == NO_ERROR) {
            return NO_ERROR;
        }
        msg = (r > 0) ? "closed-by-rpc" : "rpc error";
    } else if (signals & MX_CHANNEL_PEER_CLOSED) {
        mxrio_handle_close(rio_handler, ios);
        r = 1;
        msg = "closed-by-disconnect";
    } else {
        return NO_ERROR;
    }

    char buffer[512];
    const char* path = mkdevpath(ios->dev, buffer, sizeof(buffer));
    log(RPC_RIO, "devhost[%s] %s: %d\n", path, msg, r);

    //TODO: downref device under lock
    free(ios);
    return r;
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
mx_status_t devhost_add(mx_device_t* parent, mx_device_t* child,
                        const char* businfo, mx_handle_t resource) {
    char buffer[512];
    const char* path = mkdevpath(parent, buffer, sizeof(buffer));
    log(RPC_OUT, "devhost[%s] add '%s'\n", path, child->name);

    mx_status_t r;
    iostate_t* ios = calloc(1, sizeof(*ios));
    if (ios == NULL) {
        r = ERR_NO_MEMORY;
        goto fail;
    }

    dc_msg_t msg;
    uint32_t msglen;
    if ((r = dc_msg_pack(&msg, &msglen,
                         child->props, child->prop_count * sizeof(mx_device_prop_t),
                         child->name, businfo)) < 0) {
        goto fail;
    }
    msg.op = DC_OP_ADD_DEVICE;
    msg.protocol_id = child->protocol_id;

    // handles: remote endpoint, resource (optional)
    mx_handle_t hrpc, handle[2];
    if ((r = mx_channel_create(0, &hrpc, handle)) < 0) {
        goto fail;
    }
    handle[1] = resource;

    if ((r = dc_msg_rpc(parent->rpc, &msg, msglen,
                        handle, (resource != MX_HANDLE_INVALID) ? 2 : 1)) < 0) {
        log(ERROR, "devhost: rpc:device_add failed: %d\n", r);
    } else {
        ios->dev = child;
        ios->ph.handle = hrpc;
        ios->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
        ios->ph.func = dh_handle_dc_rpc;
        if ((r = port_watch(&dh_port, &ios->ph)) == NO_ERROR) {
            child->rpc = hrpc;
            return NO_ERROR;
        }

    }
    mx_handle_close(hrpc);
    free(ios);
    return r;

fail:
    if (resource != MX_HANDLE_INVALID) {
        mx_handle_close(resource);
    }
    free(ios);
    return r;
}

// Send message to devcoordinator informing it that this device
// is being removed.  Called under devhost api lock.
mx_status_t devhost_remove(mx_device_t* dev) {
    char buffer[512];
    const char* path = mkdevpath(dev, buffer, sizeof(buffer));
    log(RPC_OUT, "devhost[%s] remove\n", path);
    dc_msg_t msg;
    uint32_t msglen;
    mx_status_t r;
    if ((r = dc_msg_pack(&msg, &msglen, NULL, 0, NULL, NULL)) < 0) {
        return r;
    }
    msg.op = DC_OP_REMOVE_DEVICE;
    msg.protocol_id = 0;
    if ((r = dc_msg_rpc(dev->rpc, &msg, msglen, NULL, 0)) < 0) {
        log(ERROR, "devhost: rpc:device_remove failed: %d\n", r);
    }
    return r;
}

mx_status_t devhost_device_rebind(mx_device_t* dev) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t devhost_device_bind(mx_device_t* dev, const char* drv_name) {
    return ERR_NOT_SUPPORTED;
}

extern driver_api_t devhost_api;

mx_handle_t root_resource_handle;


mx_status_t devhost_start_iostate(devhost_iostate_t* ios, mx_handle_t h) {
    ios->ph.handle = h;
    ios->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    ios->ph.func = dh_handle_rio_rpc;
    return port_watch(&dh_port, &ios->ph);
}

int main(int argc, char** argv) {
    devhost_io_init();

    log(TRACE, "devhost: main()\n");

    driver_api_init(&devhost_api);

    root_ios.ph.handle = mx_get_startup_handle(PA_HND(PA_USER0, 0));
    if (root_ios.ph.handle == MX_HANDLE_INVALID) {
        log(ERROR, "devhost: rpc handle invalid\n");
        return -1;
    }

    root_resource_handle = mx_get_startup_handle(PA_HND(PA_RESOURCE, 0));
    if (root_resource_handle == MX_HANDLE_INVALID) {
        log(ERROR, "devhost: no root resource handle!\n");
    }

    mx_status_t r;
    if ((r = port_init(&dh_port)) < 0) {
        log(ERROR, "devhost: could not create port: %d\n", r);
        return -1;
    }
    if ((r = port_watch(&dh_port, &root_ios.ph)) < 0) {
        log(ERROR, "devhost: could not watch rpc channel: %d\n", r);
        return -1;
    }
    do {
        r = port_dispatch(&dh_port, MX_TIME_INFINITE);
    } while (r == NO_ERROR);
    log(ERROR, "devhost: port dispatch finished: %d\n", r);

    return 0;
}