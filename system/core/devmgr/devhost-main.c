// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"
#include "devmgr.h"
#include "devhost.h"
#include "driver-api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <launchpad/launchpad.h>

#include <magenta/ktrace.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <mxio/util.h>

#include <dirent.h>
#include <dlfcn.h>

int devhost_init(void);
int devhost_cmdline(int argc, char** argv);
int devhost_start(void);

mx_status_t devhost_add_internal(mx_device_t* parent,
                                 const char* name, uint32_t protocol_id,
                                 mx_handle_t* _hdevice, mx_handle_t* _hrpc);

extern mx_driver_t _driver_dmctl;
extern mx_handle_t _dmctl_handle;

static void init_driver(mx_driver_t* drv, bool for_root) {
        if ((drv->binding_size == 0) && (!for_root)) {
            // only load root-level drivers in the root devhost
            return;
        }
#if !ONLY_ONE_DEVHOST
        if ((drv->binding_size > 0) && (for_root)) {
            return;
        }
#endif
        driver_add(drv);
}

static void init_from_driver_info(magenta_driver_info_t* di, bool for_root) {
    mx_driver_t* drv = di->driver;
    drv->name = di->note->name;
    drv->binding = di->binding;
    drv->binding_size = di->binding_size;
    init_driver(drv, for_root);
}

static void init_loadable_drivers(bool for_root) {
    DIR* dir = opendir("/boot/lib/driver");
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
            init_from_driver_info(di, for_root);
        }
    }
    closedir(dir);
}

extern magenta_driver_info_t __start_magenta_drivers[] __WEAK;
extern magenta_driver_info_t __stop_magenta_drivers[] __WEAK;
static void init_builtin_drivers(bool for_root) {
    magenta_driver_info_t* di;
    for (di = __start_magenta_drivers; di < __stop_magenta_drivers; di++) {
        init_from_driver_info(di, for_root);
    }
}

static mx_handle_t mojo_launcher;

extern driver_api_t devhost_api;

int main(int argc, char** argv) {
    int r;
    bool as_root = false;

    driver_api_init(&devhost_api);

    if ((r = devhost_init()) < 0) {
        return r;
    }

    if ((argc > 1) && (!strcmp(argv[1], "root"))) {
        as_root = true;

        mx_status_t status = devmgr_launch_acpisvc();
        if (status != NO_ERROR) {
            return 1;
        }

        // Ignore the return value of this; if it fails, it may just be that the
        // platform doesn't support initing PCIe via ACPI.  If the platform needed
        // it, it will fail later.
        devmgr_init_pcie();
    }
    mojo_launcher = mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_USER0, ID_HLAUNCHER));
    if ((r = devhost_cmdline(argc, argv)) < 0) {
        return r;
    }
    if (as_root) {
        driver_add(&_driver_dmctl);
    }
    init_builtin_drivers(as_root);
    init_loadable_drivers(as_root);
    return devhost_start();
}

void devhost_launch_devhost(mx_device_t* parent, const char* name, uint32_t protocol_id,
                            const char* procname, int argc, char** argv) {
    mx_handle_t hdevice, hrpc;
    if (devhost_add_internal(parent, name, protocol_id, &hdevice, &hrpc) < 0) {
        return;
    }
    devmgr_launch_devhost(procname, argc, argv, hdevice, hrpc);
}

void signal_devmgr_shutdown(void) {
    devhost_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = DH_OP_SHUTDOWN;

    mx_status_t status = mx_msgpipe_write(_dmctl_handle, &msg, sizeof(msg), 0, 0, 0);
    if (status) {
        printf("Unexpected error signalling shutdown: %d\n", status);
    }
}

mx_status_t devmgr_control(const char* cmd) {
    if (!strcmp(cmd, "help")) {
        printf("dump        - dump device tree\n"
               "lsof        - list open remoteio files and devices\n"
               "crash       - crash the device manager\n"
               "poweroff    - poweroff the system\n"
               "reboot      - reboot the system\n"
               "kerneldebug - send a command to the kernel\n"
               "ktraceoff   - stop kernel tracing\n"
               "ktraceon    - start kernel tracing\n"
               "acpi-ps0    - invoke the _PS0 method on an acpi object\n"
               );
        return NO_ERROR;
    }
    if (!strcmp(cmd, "crash")) {
        *((int*)0x1234) = 42;
        return NO_ERROR;
    }
    if (!strcmp(cmd, "poweroff")) {
        signal_devmgr_shutdown();
        devmgr_poweroff();
        return ERR_NOT_SUPPORTED;
    }
    if (!strcmp(cmd, "reboot")) {
        signal_devmgr_shutdown();
        devmgr_reboot();
        return ERR_NOT_SUPPORTED;
    }
    const char* prefix = "kerneldebug ";
    if (!strncmp(cmd, prefix, strlen(prefix))) {
        const char* arg = cmd + strlen(prefix);
        return mx_debug_send_command(get_root_resource(), arg, strlen(arg));
    }
    if (!strcmp(cmd, "ktraceon")) {
        mx_ktrace_control(get_root_resource(), KTRACE_ACTION_START, KTRACE_GRP_ALL, NULL);
        return NO_ERROR;
    }
    if (!strcmp(cmd, "ktraceoff")) {
        mx_ktrace_control(get_root_resource(), KTRACE_ACTION_STOP, 0, NULL);
        mx_ktrace_control(get_root_resource(), KTRACE_ACTION_REWIND, 0, NULL);
        return NO_ERROR;
    }
    if (!strncmp(cmd, "mojo:", 5)) {
        return mx_msgpipe_write(mojo_launcher, cmd, strlen(cmd), NULL, 0, 0);
    }
    const char* ps0prefix = "acpi-ps0:";
    if (!strncmp(cmd, ps0prefix, strlen(ps0prefix))) {
        char* arg = (char*)cmd + strlen(ps0prefix);
        devmgr_acpi_ps0(arg);
        return NO_ERROR;
    }

    return ERR_NOT_SUPPORTED;
}
