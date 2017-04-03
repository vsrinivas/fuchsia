// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/devmgr.h>

#include <magenta/device/dmctl.h>
#include <magenta/ktrace.h>
#include <magenta/types.h>

#include <mxio/io.h>
#include <mxio/loader-service.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acpi.h"
#include "devhost.h"
#include "devcoordinator.h"

static mx_handle_t dmctl_handle = MX_HANDLE_INVALID;

static void signal_devmgr_shutdown(void) {
    dev_coordinator_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = DC_OP_SHUTDOWN;

    mx_status_t r;
    printf("Sending shutdown signal to devmgr...\n");
    if ((r = mx_channel_write(dmctl_handle, 0, &msg, sizeof(msg), 0, 0)) < 0) {
        printf("Unexpected error signalling shutdown: %d\n", r);
    } else if ((r = mx_object_wait_one(dmctl_handle, MX_CHANNEL_PEER_CLOSED,
                                       mx_deadline_after(MX_SEC(30)), NULL)) < 0) {
        printf("Unexpected error waiting for shutdown: %d\n", r);
    }
}

static int shutdown_async(void* arg) {
    signal_devmgr_shutdown();
    devhost_acpi_poweroff();
    return 0;
}

static int reboot_async(void* arg) {
    signal_devmgr_shutdown();
    devhost_acpi_reboot();
    return 0;
}

static mx_status_t devmgr_control(const char* cmd) {
    if (!strcmp(cmd, "help")) {
        printf("dump        - dump device tree\n"
               "poweroff    - power off the system\n"
               "shutdown    - power off the system\n"
               "reboot      - reboot the system\n"
               "kerneldebug - send a command to the kernel\n"
               "ktraceoff   - stop kernel tracing\n"
               "ktraceon    - start kernel tracing\n"
               "acpi-ps0    - invoke the _PS0 method on an acpi object\n"
               );
        return NO_ERROR;
    }
    if (!strcmp(cmd, "poweroff") || !strcmp(cmd, "shutdown")) {
        // TODO(smklein): Relocate poweroff/reboot to a non-devmgr service
        thrd_t t;
        if (thrd_create(&t, shutdown_async, NULL)) {
            devhost_acpi_poweroff();
        } else {
            thrd_detach(t);
        }
        return NO_ERROR;
    }
    if (!strcmp(cmd, "reboot")) {
        thrd_t t;
        if (thrd_create(&t, reboot_async, NULL)) {
            devhost_acpi_reboot();
        } else {
            thrd_detach(t);
        }
        return NO_ERROR;
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
    if (!strncmp(cmd, "@", 1)) {
        return mx_channel_write(get_app_launcher(), 0, cmd, strlen(cmd), NULL, 0);
    }
    const char* ps0prefix = "acpi-ps0:";
    if (!strncmp(cmd, ps0prefix, strlen(ps0prefix))) {
        char* arg = (char*)cmd + strlen(ps0prefix);
        devhost_acpi_ps0(arg);
        return NO_ERROR;
    }

    printf("dmctl: unknown command '%s'\n", cmd);
    return ERR_NOT_SUPPORTED;
}




static ssize_t dmctl_write(mx_device_t* dev, const void* buf, size_t count, mx_off_t off) {
    char cmd[1024];
    if (count < sizeof(cmd)) {
        memcpy(cmd, buf, count);
        cmd[count] = 0;
    } else {
        return ERR_INVALID_ARGS;
    }
    return devmgr_control(cmd);
}

static mxio_multiloader_t* multiloader = NULL;

static ssize_t dmctl_ioctl(mx_device_t* dev, uint32_t op,
                           const void* in_buf, size_t in_len,
                           void* out_buf, size_t out_len) {
    switch (op) {
    case IOCTL_DMCTL_GET_LOADER_SERVICE_CHANNEL:
        if (in_len != 0 || out_buf == NULL || out_len != sizeof(mx_handle_t)) {
            return ERR_INVALID_ARGS;
        }
        if (multiloader == NULL) {
            // The allocation in dmctl_init() failed.
            return ERR_NO_MEMORY;
        }
        // Create a new channel on the multiloader.
        mx_handle_t out_channel = mxio_multiloader_new_service(multiloader);
        if (out_channel < 0) {
            return out_channel;
        }
        memcpy(out_buf, &out_channel, sizeof(mx_handle_t));
        return sizeof(mx_handle_t);
    default:
        return ERR_INVALID_ARGS;
    }
}

static mx_protocol_device_t dmctl_device_proto = {
    .write = dmctl_write,
    .ioctl = dmctl_ioctl,
};

mx_status_t dmctl_init(mx_driver_t* driver) {
    // Don't try to ioctl to ourselves when this process loads libraries.
    // Call this before the device has been created; mxio_loader_service()
    // uses the device's presence as an invitation to use it.
    mxio_force_local_loader_service();

    mx_device_t* dev;
    mx_status_t s = device_create(&dev, driver, "dmctl", &dmctl_device_proto);
    if (s != NO_ERROR) {
        return s;
    }
    s = device_add(dev, driver_get_misc_device());
    if (s != NO_ERROR) {
        free(dev);
        return s;
    }
    dmctl_handle = dev->rpc;

    // Loader service init.
    s = mxio_multiloader_create("dmctl-multiloader", &multiloader);
    if (s != NO_ERROR) {
        // If this fails, IOCTL_DMCTL_GET_LOADER_SERVICE_CHANNEL will fail
        // and processes will fall back to using a local loader.
        // TODO: Make this fatal?
        printf("dmctl: cannot create multiloader context: %d\n", s);
    }

    return NO_ERROR;
}

mx_driver_t _driver_dmctl = {
    .name = "dmctl",
    .ops = {
        .init = dmctl_init,
    },
};
