// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"
#include "devmgr.h"
#include "devhost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>

#include <launchpad/launchpad.h>

#include <magenta/ktrace.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <mxio/util.h>

int devhost_init(void);
int devhost_cmdline(int argc, char** argv);
int devhost_start(void);

mx_status_t devhost_add_internal(mx_device_t* parent,
                                 const char* name, uint32_t protocol_id,
                                 mx_handle_t* _hdevice, mx_handle_t* _hrpc);

extern mx_driver_t _driver_dmctl;

extern mx_driver_t __start_builtin_drivers[] __WEAK;
extern mx_driver_t __stop_builtin_drivers[] __WEAK;

static void init_builtin_drivers(bool for_root) {
    mx_driver_t* drv;
    for (drv = __start_builtin_drivers; drv < __stop_builtin_drivers; drv++) {
        if ((drv->binding_size == 0) && (!for_root)) {
            // only load root-level drivers in the root devhost
            continue;
        }
#if !ONLY_ONE_DEVHOST
        if ((drv->binding_size > 0) && (for_root)) {
            continue;
        }
#endif
        driver_add(drv);
    }
}

static mx_handle_t mojo_launcher;

int main(int argc, char** argv) {
    int r;
    bool as_root = false;
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
        devmgr_poweroff();
        return ERR_NOT_SUPPORTED;
    }
    if (!strcmp(cmd, "reboot")) {
        devmgr_reboot();
        return ERR_NOT_SUPPORTED;
    }
    const char* prefix = "kerneldebug ";
    if (!strncmp(cmd, prefix, strlen(prefix))) {
        const char* arg = cmd + strlen(prefix);
        return mx_debug_send_command(get_root_resource(), arg, strlen(arg));
    }
    if (!strcmp(cmd, "ktraceon")) {
        mx_ktrace_control(get_root_resource(), KTRACE_ACTION_START, KTRACE_GRP_ALL);
        return NO_ERROR;
    }
    if (!strcmp(cmd, "ktraceoff")) {
        mx_ktrace_control(get_root_resource(), KTRACE_ACTION_STOP, 0);
        mx_ktrace_control(get_root_resource(), KTRACE_ACTION_REWIND, 0);
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
