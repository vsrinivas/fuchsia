// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devhost.h"
#include "devcoordinator.h"
#include "driver-info.h"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <launchpad/launchpad.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>


// driver rpc client

mx_status_t devhost_add_internal(mx_device_t* parent,
                                 const char* name, uint32_t protocol_id,
                                 mx_handle_t* _hdevice, mx_handle_t* _hrpc) {

    size_t len = strlen(name);
    if (len >= MX_DEVICE_NAME_MAX) {
        return ERR_INVALID_ARGS;
    }

    mx_handle_t hdevice[2];
    mx_handle_t hrpc[2];
    mx_status_t status;
    if ((status = mx_channel_create(0, &hdevice[0], &hdevice[1])) < 0) {
        printf("devhost_add: failed to create channel: %d\n", status);
        return status;
    }
    if ((status = mx_channel_create(0, &hrpc[0], &hrpc[1])) < 0) {
        printf("devhost_add: failed to create channel: %d\n", status);
        mx_handle_close(hdevice[0]);
        mx_handle_close(hdevice[1]);
        return status;
    }

    //printf("devhost_add(%p, %p)\n", dev, parent);
    dev_coordinator_msg_t msg;
    msg.op = DC_OP_ADD;
    msg.arg = 0;
    msg.protocol_id = protocol_id;
    memcpy(msg.name, name, len + 1);

    mx_handle_t handles[2] = { hdevice[1], hrpc[1] };
    if ((status = mx_channel_write(parent->rpc, 0, &msg, sizeof(msg), handles, 2)) < 0) {
        printf("devhost_add: failed to write channel: %d\n", status);
        mx_handle_close(hdevice[0]);
        mx_handle_close(hdevice[1]);
        mx_handle_close(hrpc[0]);
        mx_handle_close(hrpc[1]);
        return status;
    }

    *_hdevice = hdevice[0];
    *_hrpc = hrpc[0];

    // far side will close handles if this fails
    return NO_ERROR;
}

mx_status_t devhost_connect(mx_device_t* dev, mx_handle_t hdevice, mx_handle_t hrpc) {
    devhost_iostate_t* ios;
    if ((ios = create_devhost_iostate(dev)) == NULL) {
        printf("devhost_connect: cannot alloc devhost_iostate\n");
        mx_handle_close(hdevice);
        mx_handle_close(hrpc);
        return ERR_NO_MEMORY;
    }
    dev->rpc = hrpc;
    dev->ios = ios;
    mx_status_t status;
    if ((status = mxio_dispatcher_add(devhost_rio_dispatcher, hdevice, devhost_rio_handler, ios)) < 0) {
        printf("devhost_connect: cannot add to dispatcher: %d\n", status);
        mx_handle_close(hdevice);
        mx_handle_close(hrpc);
        free(ios);
        dev->rpc = 0;
        dev->ios = 0;
        return status;
    }
    return NO_ERROR;
}

mx_status_t devhost_add(mx_device_t* parent, mx_device_t* child,
                        const char* businfo, mx_handle_t resource) {
    mx_handle_t hdevice, hrpc;
    mx_status_t status;
    //devhost v1 doesn't use the resource, always consume it
    if (resource != MX_HANDLE_INVALID) {
        mx_handle_close(resource);
    }
    //printf("devhost_add(%p:%s,%p:%s)\n", parent, parent->name, child, child->name);
    if ((status = devhost_add_internal(parent, child->name, child->protocol_id,
                                       &hdevice, &hrpc)) < 0) {
        return status;
    }
    return devhost_connect(child, hdevice, hrpc);
}

mx_status_t devhost_remove(mx_device_t* dev) {
    dev_coordinator_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = DC_OP_REMOVE;
    //printf("devhost_remove(%p:%s) ios=%p\n", dev, dev->name, dev->ios);

    // ensure we don't pull the rug out from under devhost_rio_handler()
    devhost_iostate_t* ios = dev->ios;
    mtx_lock(&ios->lock);
    dev->ios = NULL;
    ios->dev = NULL;
    mtx_unlock(&ios->lock);

    mx_channel_write(dev->rpc, 0, &msg, sizeof(msg), 0, 0);
    mx_handle_close(dev->rpc);
    dev->rpc = 0;
    return NO_ERROR;
}




// driver loading

#define DRIVER_NAME_LEN_MAX 64

#define DRV_STATE_NEED_LOAD 0
#define DRV_STATE_NEED_INIT 1
#define DRV_STATE_READY 2
#define DRV_STATE_ERROR 3

typedef struct {
    mx_driver_t drv;
    struct list_node node;
    mtx_t lock;
    uint32_t state;
    const char* libname;
} driver_record_t;

static list_node_t driver_list = LIST_INITIAL_VALUE(driver_list);

mx_status_t devhost_load_driver(mx_driver_t* drv) {
    driver_record_t* rec = (void*) drv;
    mx_status_t status = NO_ERROR;

    mtx_lock(&rec->lock);
    switch (rec->state) {
    case DRV_STATE_NEED_LOAD: {
        void* dl = dlopen(rec->libname, RTLD_NOW);
        if (dl == NULL) {
            printf("devhost: cannot load '%s': %s\n", rec->libname, dlerror());
            status = ERR_IO;
            break;
        }

        magenta_driver_info_t* di = dlsym(dl, "__magenta_driver__");
        if (di == NULL) {
            printf("devhost: driver '%s' missing __magenta_driver__ symbol\n", rec->libname);
            status = ERR_IO;
            break;
        }
        if (!di->driver->ops) {
            printf("devhost: driver '%s' has NULL ops\n", rec->libname);
            status = ERR_INVALID_ARGS;
            break;
        }
        if (di->driver->ops->version != DRIVER_OPS_VERSION) {
            printf("devhost: driver '%s' has bad driver ops version %" PRIx64
                    ", expecting %" PRIx64 "\n", rec->libname,
                    di->driver->ops->version, DRIVER_OPS_VERSION);
            status = ERR_INVALID_ARGS;
            break;
        }

        printf("devhost: loaded '%s'\n", rec->libname);
        rec->drv.ops = di->driver->ops;
        rec->drv.flags = di->driver->flags;
        // fallthrough
    }
    case DRV_STATE_NEED_INIT:
        if (rec->drv.ops->init) {
            status = rec->drv.ops->init(drv);
            if (status < 0) {
                printf("devhost: driver '%s' failed in init: %d\n", rec->libname, status);
                break;
            }
        }
        break;
    case DRV_STATE_READY:
        break;
    case DRV_STATE_ERROR:
        status = ERR_NOT_FOUND;
        break;
    }

    if (status < 0) {
        rec->state = DRV_STATE_ERROR;
    } else {
        rec->state = DRV_STATE_READY;
    }
    mtx_unlock(&rec->lock);
    return status;
}

static bool is_driver_disabled(const char* name) {
    // driver.<driver_name>.disable
    char opt[16 + DRIVER_NAME_LEN_MAX];
    snprintf(opt, 16 + DRIVER_NAME_LEN_MAX, "driver.%s.disable", name);
    return getenv(opt) != NULL;
}

static void found_driver(magenta_note_driver_t* note, mx_bind_inst_t* bi, void* cookie) {
    // ensure strings are terminated
    note->name[sizeof(note->name) - 1] = 0;
    note->vendor[sizeof(note->vendor) - 1] = 0;
    note->version[sizeof(note->version) - 1] = 0;

    if (is_driver_disabled(note->name)) {
        return;
    }

    const char* libname = cookie;
    size_t pathlen = strlen(libname) + 1;
    size_t namelen = strlen(note->name) + 1;
    size_t bindlen = note->bindcount * sizeof(mx_bind_inst_t);
    size_t len = sizeof(driver_record_t) + bindlen + pathlen + namelen;

    driver_record_t* rec;
    if ((rec = malloc(len)) == NULL) {
        return;
    }

    memset(rec, 0, sizeof(driver_record_t));
    mtx_init(&rec->lock, mtx_plain);
    rec->drv.binding_size = bindlen;
    rec->drv.binding = (void*) (rec + 1);
    rec->libname = (void*) (rec->drv.binding + note->bindcount);
    rec->drv.name = rec->libname + pathlen;
    rec->state = DRV_STATE_NEED_LOAD;

    memcpy((void*) rec->drv.binding, bi, bindlen);
    memcpy((void*) rec->libname, libname, pathlen);
    memcpy((void*) rec->drv.name, note->name, namelen);

#if VERBOSE_DRIVER_LOAD
    printf("found driver: %s\n", (char*) cookie);
    printf("        name: %s\n", note->name);
    printf("      vendor: %s\n", note->vendor);
    printf("     version: %s\n", note->version);
    printf("     binding:\n");
    for (size_t n = 0; n < note->bindcount; n++) {
        printf("         %03zd: %08x %08x\n", n, bi[n].op, bi[n].arg);
    }
#endif

    if (note->version[0] == '!') {
        // debugging / development hack
        // prioritize drivers with version "!..." over others
        list_add_head(&driver_list, &rec->node);
    } else {
        list_add_tail(&driver_list, &rec->node);
    }
}

// device binding program that pure (parentless)
// misc devices use to get published in the
// primary devhost
static struct mx_bind_inst misc_device_binding =
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_MISC_PARENT);

static bool is_misc_driver(mx_driver_t* drv) {
    return (drv->binding_size == sizeof(misc_device_binding)) &&
        (memcmp(&misc_device_binding, drv->binding, sizeof(misc_device_binding)) == 0);
}

static void init_driver(mx_driver_t* drv, bool for_root) {
    if ((drv->binding_size == 0) || is_misc_driver(drv)) {
        // no-binding drivers or pure misc drivers are
        // only loaded in the root devhost
        if (!for_root) {
            return;
        }
    }

    // Built-in drivers need their init hook called
    // *before* being added. Loadable drivers get
    // init'd just after load and before they're first
    // bound
    driver_record_t* rec = (void*) drv;
    if (rec->state == DRV_STATE_NEED_INIT) {
        if (devhost_load_driver(drv) < 0) {
            return;
        }
    }

    driver_add(drv);
}

static void init_loadable_drivers(bool for_root) {
    driver_record_t* rec;
    list_for_every_entry(&driver_list, rec, driver_record_t, node) {
        init_driver(&rec->drv, for_root);
    }
}

static void find_loadable_drivers(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        return;
    }
    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') {
            continue;
        }

        char libname[256 + 32];
        if (de->d_name[0] == '.') {
            continue;
        }
        int r = snprintf(libname, sizeof(libname), "driver/%s", de->d_name);
        if ((r < 0) || (r >= (int)sizeof(libname))) {
            continue;
        }

        int fd;
        if ((fd = openat(dirfd(dir), de->d_name, O_RDONLY)) < 0) {
            continue;
        }
        mx_status_t status = read_driver_info(fd, libname, found_driver);
        close(fd);

        if (status) {
            if (status == ERR_NOT_FOUND) {
                printf("devhost: no driver info in '%s'\n", libname);
            } else {
                printf("devhost: error reading info from '%s'\n", libname);
            }
        }
    }
    closedir(dir);
}

static void init_from_driver_info(magenta_driver_info_t* di, bool for_root) {
    driver_record_t* rec;
    if ((rec = calloc(1, sizeof(driver_record_t))) == NULL) {
        return;
    }

    mtx_init(&rec->lock, mtx_plain);
    memcpy(&rec->drv, di->driver, sizeof(mx_driver_t));
    rec->drv.name = di->note->name;
    rec->state = DRV_STATE_NEED_INIT;
    init_driver(&rec->drv, for_root);
}


extern magenta_driver_info_t __start_magenta_drivers[] __WEAK;
extern magenta_driver_info_t __stop_magenta_drivers[] __WEAK;
static void init_builtin_drivers(bool for_root) {
    magenta_driver_info_t* di;
    for (di = __start_magenta_drivers; di < __stop_magenta_drivers; di++) {
        if (is_driver_disabled(di->note->name)) continue;
        init_from_driver_info(di, for_root);
    }
}

extern mx_driver_t _driver_dmctl;
// FIXME(yky,teisenbe): remove when real acpi bus driver goes in
extern mx_driver_t _driver_acpi_root;

void devhost_init_drivers(bool as_root) {
    if (as_root) {
        // dmctl must be loaded first as the dynamic loader
        // and other core services depend on it
        _driver_dmctl.ops->init(&_driver_dmctl);

        // acpi must be loaded second until we get the bus
        // manager startup process rationalized
        _driver_acpi_root.ops->init(&_driver_acpi_root);
    }
    init_builtin_drivers(as_root);
    find_loadable_drivers("/system/lib/driver");
    find_loadable_drivers("/boot/lib/driver");
    init_loadable_drivers(as_root);
}
