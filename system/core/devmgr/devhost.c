// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <magenta/dlfcn.h>
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

static mx_status_t dh_handle_dc_rpc(port_handler_t* ph, mx_signals_t signals, uint32_t evt);

static port_t dh_port;

typedef struct devhost_iostate iostate_t;

static iostate_t root_ios = {
    .ph = {
        .waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
        .func = dh_handle_dc_rpc,
    },
};

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

static uint32_t logflagval(char* flag) {
    if (!strcmp(flag, "error")) {
        return DDK_LOG_ERROR;
    }
    if (!strcmp(flag, "info")) {
        return DDK_LOG_INFO;
    }
    if (!strcmp(flag, "trace")) {
        return DDK_LOG_TRACE;
    }
    if (!strcmp(flag, "spew")) {
        return DDK_LOG_SPEW;
    }
    if (!strcmp(flag, "debug1")) {
        return DDK_LOG_DEBUG1;
    }
    if (!strcmp(flag, "debug2")) {
        return DDK_LOG_DEBUG2;
    }
    if (!strcmp(flag, "debug3")) {
        return DDK_LOG_DEBUG3;
    }
    if (!strcmp(flag, "debug4")) {
        return DDK_LOG_DEBUG4;
    }
    return strtoul(flag, NULL, 0);
}

static void logflag(char* flag, uint32_t* flags) {
    if (*flag == '+') {
        *flags |= logflagval(flag + 1);
    } else if (*flag == '-') {
        *flags &= ~logflagval(flag + 1);
    }
}

static mx_status_t dh_find_driver(const char* libname, mx_handle_t vmo, mx_driver_t** out) {
    // check for already-loaded driver first
    mx_driver_t* drv;
    list_for_every_entry(&dh_drivers, drv, mx_driver_t, node) {
        if (!strcmp(libname, drv->libname)) {
            *out = drv;
            mx_handle_close(vmo);
            return drv->status;
        }
    }

    int len = strlen(libname) + 1;
    drv = calloc(1, sizeof(mx_driver_t) + len);
    if (drv == NULL) {
        mx_handle_close(vmo);
        return MX_ERR_NO_MEMORY;
    }
    memcpy((void*) (drv + 1), libname, len);
    drv->libname = (const char*) (drv + 1);
    list_add_tail(&dh_drivers, &drv->node);
    *out = drv;

    void* dl = dlopen_vmo(vmo, RTLD_NOW);
    if (dl == NULL) {
        log(ERROR, "devhost: cannot load '%s': %s\n", libname, dlerror());
        drv->status = MX_ERR_IO;
        goto done;
    }

    const magenta_driver_note_t* dn = dlsym(dl, "__magenta_driver_note__");
    if (dn == NULL) {
        log(ERROR, "devhost: driver '%s' missing __magenta_driver_note__ symbol\n", libname);
        drv->status = MX_ERR_IO;
        goto done;
    }
    mx_driver_rec_t* dr = dlsym(dl, "__magenta_driver_rec__");
    if (dr == NULL) {
        log(ERROR, "devhost: driver '%s' missing __magenta_driver_rec__ symbol\n", libname);
        drv->status = MX_ERR_IO;
        goto done;
    }
    if (!dr->ops) {
        log(ERROR, "devhost: driver '%s' has NULL ops\n", libname);
        drv->status = MX_ERR_INVALID_ARGS;
        goto done;
    }
    if (dr->ops->version != DRIVER_OPS_VERSION) {
        log(ERROR, "devhost: driver '%s' has bad driver ops version %" PRIx64
            ", expecting %" PRIx64 "\n", libname,
            dr->ops->version, DRIVER_OPS_VERSION);
        drv->status = MX_ERR_INVALID_ARGS;
        goto done;
    }

    drv->name = dn->payload.name;
    drv->ops = dr->ops;
    dr->driver = drv;

    // check for dprintf log level flags
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "driver.%s.log", drv->name);
    char* log = getenv(tmp);
    if (log) {
        while (log) {
            char* sep = strchr(log, ',');
            if (sep) {
                *sep = 0;
                logflag(log, &dr->log_flags);
                *sep = ',';
                log = sep + 1;
            } else {
                logflag(log, &dr->log_flags);
                break;
            }
        }
        log(INFO, "devhost: driver '%s': log flags set to: 0x%x\n", drv->name, dr->log_flags);
    }

    if (drv->ops->init) {
        drv->status = drv->ops->init(&drv->ctx);
        if (drv->status < 0) {
            log(ERROR, "devhost: driver '%s' failed in init: %d\n",
                libname, drv->status);
        }
    } else {
        drv->status = MX_OK;
    }

done:
    mx_handle_close(vmo);
    return drv->status;
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

    mx_status_t r;
    if ((r = devhost_rio_handler(msg, ios)) < 0) {
        if (r != ERR_DISPATCHER_INDIRECT) {
            log(ERROR, "devhost: OPEN failed: %d\n", r);
        }
    }
}

static mx_status_t dh_handle_rpc_read(mx_handle_t h, iostate_t* ios) {
    dc_msg_t msg;
    mx_handle_t hin[3];
    uint32_t msize = sizeof(msg);
    uint32_t hcount = 3;

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
            r = MX_ERR_INTERNAL;
            goto fail;
        }
        log(RPC_RIO, "devhost[%s] remoteio OPEN\n", path);
        dh_handle_open((void*) &msg, msize, hin[0], ios);
        return MX_OK;
    }

    const void* data;
    const char* name;
    const char* args;
    if ((r = dc_msg_unpack(&msg, msize, &data, &name, &args)) < 0) {
        goto fail;
    }
    switch (msg.op) {
    case DC_OP_CREATE_DEVICE_STUB:
        log(RPC_IN, "devhost[%s] create device stub drv='%s'\n", path, name);
        if (hcount < 1 || hcount > 2) {
            printf("HCOUNT %d\n", hcount);
            r = MX_ERR_INVALID_ARGS;
            goto fail;
        }
        iostate_t* newios = calloc(1, sizeof(iostate_t));
        if (newios == NULL) {
            r = MX_ERR_NO_MEMORY;
            break;
        }

        //TODO: dev->ops and other lifecycle bits
        // no name means a dummy shadow device
        if ((newios->dev = calloc(1, sizeof(mx_device_t))) == NULL) {
            free(newios);
            r = MX_ERR_NO_MEMORY;
            break;
        }
        mx_device_t* dev = newios->dev;
        memcpy(dev->name, "shadow", 7);
        dev->protocol_id = msg.protocol_id;
        dev->ops = &device_default_ops;
        dev->rpc = hin[0];
        dev->resource = (hcount == 2 ? hin[1] : MX_HANDLE_INVALID);
        dev->refcount = 1;
        list_initialize(&dev->children);
        list_initialize(&dev->instances);

        newios->ph.handle = hin[0];
        newios->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
        newios->ph.func = dh_handle_dc_rpc;
        if ((r = port_wait(&dh_port, &newios->ph)) < 0) {
            free(newios->dev);
            free(newios);
            break;
        }
        log(RPC_IN, "devhost[%s] created '%s' ios=%p\n", path, name, newios);
        return MX_OK;

    case DC_OP_CREATE_DEVICE: {
        // This does not operate under the devhost api lock,
        // since the newly created device is not visible to
        // any API surface until a driver is bound to it.
        // (which can only happen via another message on this thread)
        log(RPC_IN, "devhost[%s] create device drv='%s' args='%s'\n", path, name, args);

        // hin: rpc, vmo, optional-rsrc
        if (hcount == 2) {
            hin[2] = MX_HANDLE_INVALID;
        } else if (hcount != 3) {
            r = MX_ERR_INVALID_ARGS;
            break;
        }
        iostate_t* newios = calloc(1, sizeof(iostate_t));
        if (newios == NULL) {
            r = MX_ERR_NO_MEMORY;
            break;
        }

        // named driver -- ask it to create the device
        mx_driver_t* drv;
        if ((r = dh_find_driver(name, hin[1], &drv)) < 0) {
            free(newios);
            log(ERROR, "devhost[%s] driver load failed: %d\n", path, r);
            break;
        }
        if (drv->ops->create) {
            // magic cookie for device create handshake
            mx_device_t parent = {
                .name = "device_create dummy",
                .owner = drv,
            };
            device_create_setup(&parent);
            if ((r = drv->ops->create(drv->ctx, &parent, "shadow", args, hin[2])) < 0) {
                log(ERROR, "devhost[%s] driver create() failed: %d\n", path, r);
                device_create_setup(NULL);
                break;
            }
            if ((newios->dev = device_create_setup(NULL)) == NULL) {
                log(ERROR, "devhost[%s] driver create() failed to create a device!", path);
                r = MX_ERR_BAD_STATE;
                break;
            }
        } else {
            log(ERROR, "devhost[%s] driver create() not supported\n", path);
            r = MX_ERR_NOT_SUPPORTED;
            break;
        }
        //TODO: inform devcoord

        newios->dev->rpc = hin[0];
        newios->ph.handle = hin[0];
        newios->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
        newios->ph.func = dh_handle_dc_rpc;
        if ((r = port_wait(&dh_port, &newios->ph)) < 0) {
            free(newios);
            break;
        }
        log(RPC_IN, "devhost[%s] created '%s' ios=%p\n", path, name, newios);
        return MX_OK;
    }

    case DC_OP_BIND_DRIVER:
        if (hcount != 1) {
            r = MX_ERR_INVALID_ARGS;
            break;
        }
        //TODO: api lock integration
        log(RPC_IN, "devhost[%s] bind driver '%s'\n", path, name);
        mx_driver_t* drv;
        if (ios->dev->flags & DEV_FLAG_DEAD) {
            log(ERROR, "devhost[%s] bind to removed device disallowed\n", path);
            r = MX_ERR_IO_NOT_PRESENT;
        } else if ((r = dh_find_driver(name, hin[0], &drv)) < 0) {
            log(ERROR, "devhost[%s] driver load failed: %d\n", path, r);
        } else {
            void* cookie = NULL;
            if (drv->ops->bind) {
                r = drv->ops->bind(drv->ctx, ios->dev, &cookie);
            } else {
                r = MX_ERR_NOT_SUPPORTED;
            }
            if (r < 0) {
                log(ERROR, "devhost[%s] bind driver '%s' failed: %d\n", path, name, r);
            } else {
                //TODO: Best behaviour for multibind? maybe retire "owner"?
                //      For now this is extermely rare, so we mostly can ignore
                //      it.
                if (drv->ops->unbind || cookie) {
                    log(INFO, "devhost[%s] driver '%s' unbind=%p, cookie=%p\n",
                        path, name, drv->ops->unbind, cookie);

                    DM_LOCK();
                    if (ios->dev->owner) {
                        log(ERROR, "devhost[%s] driver '%s' device already owned!\n", path, name);
                    } else {
                        ios->dev->owner = drv;
                        ios->dev->owner_cookie = cookie;
                        ios->dev->refcount++;
                    }
                    DM_UNLOCK();
                }
            }
        }
        dc_msg_t reply = {
            .txid = 0,
            .op = DC_OP_STATUS,
            .status = r,
        };
        mx_channel_write(h, 0, &reply, sizeof(reply), NULL, 0);
        return MX_OK;

    default:
        log(ERROR, "devhost[%s] invalid rpc op %08x\n", path, msg.op);
        r = MX_ERR_NOT_SUPPORTED;
    }

fail:
    while (hcount > 0) {
        mx_handle_close(hin[--hcount]);
    }
    return r;
}

// handles devcoordinator rpc
static mx_status_t dh_handle_dc_rpc(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
    iostate_t* ios = ios_from_ph(ph);

    if (evt != 0) {
        // we send an event to request the destruction
        // of an iostate, to ensure that's the *last*
        // packet about the iostate that we get
        free(ios);
        return MX_ERR_STOP;
    }
    if (ios->dead) {
        // ports does not let us cancel packets that are
        // alread in the queue, so the dead flag enables us
        // to ignore them
        return MX_ERR_STOP;
    }
    if (signals & MX_CHANNEL_READABLE) {
        mx_status_t r = dh_handle_rpc_read(ph->handle, ios);
        if (r != MX_OK) {
            log(ERROR, "devhost: devmgr rpc unhandleable ios=%p r=%d. fatal.\n", ios, r);
            exit(0);
        }
        return r;
    }
    if (signals & MX_CHANNEL_PEER_CLOSED) {
        log(ERROR, "devhost: devmgr disconnected! fatal. (ios=%p)\n", ios);
        exit(0);
    }
    log(ERROR, "devhost: no work? %08x\n", signals);
    return MX_OK;
}

// handles remoteio rpc
static mx_status_t dh_handle_rio_rpc(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
    iostate_t* ios = ios_from_ph(ph);

    mx_status_t r;
    mxrio_msg_t msg;
    if (signals & MX_CHANNEL_READABLE) {
        if ((r = mxrio_handle_rpc(ph->handle, &msg, devhost_rio_handler, ios)) == MX_OK) {
            return MX_OK;
        }
    } else if (signals & MX_CHANNEL_PEER_CLOSED) {
        mxrio_handle_close(devhost_rio_handler, ios);
        r = MX_ERR_STOP;
    } else {
        printf("dh_handle_rio_rpc: invalid signals %x\n", signals);
        exit(0);
    }

    // We arrive here if handle_rpc was a clean close (ERR_DISPATCHER_DONE),
    // or close-due-to-error (non-MX_OK), or if the channel was closed
    // out from under us (MX_ERR_STOP).  In all cases, the ios's reference to
    // the device was released, and will no longer be used, so we will free
    // it before returning.
    mx_handle_close(ios->ph.handle);
    free(ios);
    return r;
}

#define LOGBUF_MAX (MX_LOG_RECORD_MAX - sizeof(mx_log_record_t))

static mx_handle_t devhost_log_handle;

static ssize_t _devhost_log_write(uint32_t flags, const void* _data, size_t len) {
    static thread_local struct {
        uint32_t next;
        mx_handle_t handle;
        char data[LOGBUF_MAX];
    }* ctx = NULL;

    if (ctx == NULL) {
        if ((ctx = calloc(1, sizeof(*ctx))) == NULL) {
            return len;
        }
        ctx->handle = devhost_log_handle;
    }

    const char* data = _data;
    size_t r = len;

    while (len-- > 0) {
        char c = *data++;
        if (c == '\n') {
            if (ctx->next) {
flush_ctx:
                mx_log_write(ctx->handle, ctx->next, ctx->data, flags);
                ctx->next = 0;
            }
            continue;
        }
        if (c < ' ') {
            continue;
        }
        ctx->data[ctx->next++] = c;
        if (ctx->next == LOGBUF_MAX) {
            goto flush_ctx;
        }
    }
    return r;
}

__EXPORT void driver_printf(uint32_t flags, const char* fmt, ...) {
    char buffer[512];
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    if (r > (int)sizeof(buffer)) {
        r = sizeof(buffer);
    }

    _devhost_log_write(flags, buffer, r);
}

static ssize_t devhost_log_write(void* cookie, const void* data, size_t len) {
    return _devhost_log_write(0, data, len);
}

static void devhost_io_init(void) {
    if (mx_log_create(0, &devhost_log_handle) < 0) {
        return;
    }
    mxio_t* io;
    if ((io = mxio_output_create(devhost_log_write, NULL)) == NULL) {
        return;
    }
    close(1);
    mxio_bind_to_fd(io, 1, 0);
    dup2(1, 2);
}

// Send message to devcoordinator asking to add child device to
// parent device.  Called under devhost api lock.
mx_status_t devhost_add(mx_device_t* parent, mx_device_t* child,
                        const char* businfo, mx_handle_t resource,
                        const mx_device_prop_t* props, uint32_t prop_count) {
    char buffer[512];
    const char* path = mkdevpath(parent, buffer, sizeof(buffer));
    log(RPC_OUT, "devhost[%s] add '%s'\n", path, child->name);

    const char* libname = child->driver->libname;
    size_t namelen = strlen(libname) + strlen(child->name) + 2;
    char name[namelen];
    snprintf(name, namelen, "%s,%s", libname, child->name);

    mx_status_t r;
    iostate_t* ios = calloc(1, sizeof(*ios));
    if (ios == NULL) {
        r = MX_ERR_NO_MEMORY;
        goto fail;
    }

    dc_msg_t msg;
    uint32_t msglen;
    if ((r = dc_msg_pack(&msg, &msglen,
                         props, prop_count * sizeof(mx_device_prop_t),
                         name, businfo)) < 0) {
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

    dc_status_t rsp;
    if ((r = dc_msg_rpc(parent->rpc, &msg, msglen,
                        handle, (resource != MX_HANDLE_INVALID) ? 2 : 1,
                        &rsp, sizeof(rsp))) < 0) {
        log(ERROR, "devhost[%s] add '%s': rpc failed: %d\n", path, child->name, r);
    } else {
        ios->dev = child;
        ios->ph.handle = hrpc;
        ios->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
        ios->ph.func = dh_handle_dc_rpc;
        if ((r = port_wait(&dh_port, &ios->ph)) == MX_OK) {
            child->rpc = hrpc;
            child->ios = ios;
            return MX_OK;
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

static mx_status_t devhost_rpc(mx_device_t* dev, uint32_t op,
                               const char* args, const char* opname,
                               dc_status_t* rsp, size_t rsp_len) {
    char buffer[512];
    const char* path = mkdevpath(dev, buffer, sizeof(buffer));
    log(RPC_OUT, "devhost[%s] %s args='%s'\n", path, opname, args ? args : "");
    dc_msg_t msg;
    uint32_t msglen;
    mx_status_t r;
    if ((r = dc_msg_pack(&msg, &msglen, NULL, 0, NULL, args)) < 0) {
        return r;
    }
    msg.op = op;
    msg.protocol_id = 0;
    if ((r = dc_msg_rpc(dev->rpc, &msg, msglen, NULL, 0, rsp, rsp_len)) < 0) {
        log(ERROR, "devhost: rpc:%s failed: %d\n", opname, r);
    }
    return r;
}

// Send message to devcoordinator informing it that this device
// is being removed.  Called under devhost api lock.
mx_status_t devhost_remove(mx_device_t* dev) {
    devhost_iostate_t* ios = dev->ios;
    if (ios == NULL) {
        log(ERROR, "removing device %p, ios is NULL\n", dev);
        return MX_ERR_INTERNAL;
    }

    log(DEVLC, "removing device %p, ios %p\n", dev, ios);

    // Make this iostate inactive (stop accepting RPCs for it)
    //
    // If the remove is happening on a different thread than
    // the rpc handler, the handler might observe the peer
    // before devhost_simple_rpc() returns.
    ios->dev = NULL;
    ios->dead = true;

    // ensure we get no further events
    //TODO: this does not work yet, ports limitation
    port_cancel(&dh_port, &ios->ph);
    ios->ph.handle = MX_HANDLE_INVALID;
    dev->ios = NULL;

    dc_status_t rsp;
    devhost_rpc(dev, DC_OP_REMOVE_DEVICE, NULL, "remove-device", &rsp, sizeof(rsp));

    // shut down our rpc channel
    mx_handle_close(dev->rpc);
    dev->rpc = MX_HANDLE_INVALID;

    // queue an event to destroy the iostate
    port_queue(&dh_port, &ios->ph, 1);

    return MX_OK;
}

mx_status_t devhost_get_topo_path(mx_device_t* dev, char* path, size_t max, size_t* actual) {
    struct {
        dc_status_t rsp;
        char path[DC_PATH_MAX];
    } reply;
    mx_status_t r;
    if ((r = devhost_rpc(dev, DC_OP_GET_TOPO_PATH, NULL, "get-topo-path",
                         &reply.rsp, sizeof(reply))) < 0) {
        return r;
    }
    reply.path[DC_PATH_MAX - 1] = 0;
    size_t len = strlen(reply.path) + 1;
    if (len > max) {
        return MX_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(path, reply.path, len);
    *actual = len;
    return MX_OK;
}

mx_status_t devhost_device_bind(mx_device_t* dev, const char* drv_libname) {
    dc_status_t rsp;
    return devhost_rpc(dev, DC_OP_BIND_DEVICE, drv_libname, "bind-device", &rsp, sizeof(rsp));
}


mx_handle_t root_resource_handle;


mx_status_t devhost_start_iostate(devhost_iostate_t* ios, mx_handle_t h) {
    ios->ph.handle = h;
    ios->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    ios->ph.func = dh_handle_rio_rpc;
    return port_wait(&dh_port, &ios->ph);
}

__EXPORT int device_host_main(int argc, char** argv) {
    devhost_io_init();

    log(TRACE, "devhost: main()\n");

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
    if ((r = port_wait(&dh_port, &root_ios.ph)) < 0) {
        log(ERROR, "devhost: could not watch rpc channel: %d\n", r);
        return -1;
    }

    r = port_dispatch(&dh_port, MX_TIME_INFINITE, false);
    log(ERROR, "devhost: port dispatch finished: %d\n", r);

    return 0;
}
