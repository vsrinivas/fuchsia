// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devhost.h"
#include "devcoordinator.h"

#include <dirent.h>
#include <dlfcn.h>
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

mx_status_t devhost_add(mx_device_t* parent, mx_device_t* child) {
    mx_handle_t hdevice, hrpc;
    mx_status_t status;
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

// device binding program that pure (parentless)
// misc devices use to get published in the
// primary devhost
static struct mx_bind_inst misc_device_binding =
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_MISC_PARENT);

static void init_driver(mx_driver_t* drv, bool for_root) {
    if ((drv->binding_size == 0) && (!for_root)) {
        // only load root-level drivers in the root devhost
        return;
    }
#if !ONLY_ONE_DEVHOST
    if (for_root && (drv->binding_size > 0)) {
        if ((drv->binding_size != sizeof(misc_device_binding)) ||
            memcmp(&misc_device_binding, drv->binding, sizeof(misc_device_binding))) {
            return;
        }
    }
#endif
    driver_add(drv);
}

static bool is_driver_disabled(magenta_driver_info_t* di) {
    // driver.<driver_name>.disable
    char opt[16 + DRIVER_NAME_LEN_MAX];
    snprintf(opt, 16 + DRIVER_NAME_LEN_MAX, "driver.%s.disable", di->note->name);
    return getenv(opt) != NULL;
}

static void init_from_driver_info(magenta_driver_info_t* di, bool for_root) {
    mx_driver_t* drv = di->driver;
    drv->name = di->note->name;
    drv->binding = di->binding;
    drv->binding_size = di->binding_size;
    init_driver(drv, for_root);
}

static list_node_t driver_list = LIST_INITIAL_VALUE(driver_list);

static void load_loadable_drivers(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        return;
    }
    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
        char libname[256 + 32];
        if (de->d_name[0] == '.') {
            continue;
        }
        int r = snprintf(libname, sizeof(libname), "driver/%s", de->d_name);
        if ((r < 0) || (r >= (int)sizeof(libname))) {
            continue;
        }
        void* dl = dlopen(libname, RTLD_NOW);
        if (dl == NULL) {
            printf("devhost: cannot load '%s': %s\n", libname, dlerror());
            continue;
        }
        magenta_driver_info_t* di = dlsym(dl, "__magenta_driver__");
        if (di == NULL) {
            printf("devhost: driver '%s' missing __magenta_driver__ symbol\n", libname);
        } else {
            if (is_driver_disabled(di)) continue;
            if (di->note->version[0] == '!') {
                // debugging / development hack
                // prioritize drivers with version "!..." over others
                list_add_head(&driver_list, &di->node);
            } else {
                list_add_tail(&driver_list, &di->node);
            }
        }
    }
    closedir(dir);
}

static void init_loaded_drivers(bool for_root) {
    // We have to dlopen() all drivers before init'ing them, because
    // drivers can start threads that map memory which can interfere
    // with further dlopen() operations.
    magenta_driver_info_t* di;
    list_for_every_entry(&driver_list, di, magenta_driver_info_t, node) {
        init_from_driver_info(di, for_root);
    }
}

extern magenta_driver_info_t __start_magenta_drivers[] __WEAK;
extern magenta_driver_info_t __stop_magenta_drivers[] __WEAK;
static void init_builtin_drivers(bool for_root) {
    magenta_driver_info_t* di;
    for (di = __start_magenta_drivers; di < __stop_magenta_drivers; di++) {
        if (is_driver_disabled(di)) continue;
        init_from_driver_info(di, for_root);
    }
}

extern mx_driver_t _driver_dmctl;
// FIXME(yky,teisenbe): remove when real acpi bus driver goes in
extern mx_driver_t _driver_acpi_root;

void devhost_init_drivers(bool as_root) {
    if (as_root) {
        driver_add(&_driver_dmctl);
        // FIXME(yky,teisenbe): remove when real acpi bus driver goes in
        driver_add(&_driver_acpi_root);
    }
    init_builtin_drivers(as_root);
    load_loadable_drivers("/system/lib/driver");
    load_loadable_drivers("/boot/lib/driver");
    init_loaded_drivers(as_root);
}
