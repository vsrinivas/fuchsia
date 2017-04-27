// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"

// only for devmgr_launch_devhost()
#include "devmgr.h"

#include "devhost.h"
#include <driver/driver-api.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <launchpad/launchpad.h>

#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>
#include <magenta/types.h>

#include <mxio/util.h>

static mx_handle_t job_handle;
static mx_handle_t app_launcher;
static mx_handle_t sysinfo_job_root;

mx_handle_t get_app_launcher(void) {
    return app_launcher;
}

// used by builtin sysinfo driver
mx_handle_t get_sysinfo_job_root(void) {
    mx_handle_t h;
    if (mx_handle_duplicate(sysinfo_job_root, MX_RIGHT_SAME_RIGHTS, &h) < 0) {
        return MX_HANDLE_INVALID;
    } else {
        return h;
    }
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
    close(STDOUT_FILENO);
    mxio_bind_to_fd(logger, STDOUT_FILENO, 0);
    dup2(STDOUT_FILENO, STDERR_FILENO);
}


mx_handle_t root_resource_handle;

static mx_driver_t root_driver = {
    .name = "root",
};
static mx_protocol_device_t root_ops;

static mx_handle_t hdevice;
static mx_handle_t hrpc;
static mx_handle_t hacpi;

mx_handle_t devhost_get_hacpi(void) {
#if ONLY_ONE_DEVHOST
    return devmgr_acpi_clone();
#else
    return hacpi;
#endif
}

// Give core builtin drivers some control over where they publish
// Drivers in the non-root devhost do not have access to this.

static mx_device_t* the_root_device;
static mx_device_t* the_misc_device;

mx_device_t* driver_get_root_device(void) {
    return the_root_device;
}

mx_device_t* driver_get_misc_device(void) {
    return the_misc_device;
}

static int devhost_init(void) {
    job_handle = mx_job_default();
    sysinfo_job_root = mx_get_startup_handle(PA_HND(PA_USER0, ID_HJOBROOT));
    app_launcher = mx_get_startup_handle(PA_HND(PA_USER0, ID_HLAUNCHER));
    root_resource_handle = mx_get_startup_handle(PA_HND(PA_RESOURCE, 0));
    hdevice = mx_get_startup_handle(PA_HND(PA_USER0, ID_HDEVICE));
    hrpc = mx_get_startup_handle(PA_HND(PA_USER0, ID_HRPC));
    hacpi = mx_get_startup_handle(PA_HND(PA_USER0, ID_HACPI));

    //TODO: figure out why we need to do this
    mx_handle_t vmo = mx_get_startup_handle(PA_HND(PA_VMO_VDSO, 0));
    vmo = launchpad_set_vdso_vmo(vmo);

    if (root_resource_handle <= 0) {
        fprintf(stderr, "devhost: missing root resource handle\n");
        return -1;
    }
    if ((hdevice <= 0) || (hrpc <= 0)) {
        fprintf(stderr, "devhost: missing device handle(s)\n");
        return -1;
    }
    if (hacpi <= 0) {
        fprintf(stderr, "devhost: missing acpi handle\n");
    }

    mxio_dispatcher_create(&devhost_rio_dispatcher, mxrio_handler);
    return 0;
}

static bool as_root = false;

static int devhost_cmdline(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "devhost: missing command line argument\n");
        return -1;
    }

    mx_device_t* dev;
    mx_status_t status;
    if (!strcmp(argv[1], "root")) {
        // The "root" devhost is launched by devmgr and currently hosts
        // the drivers without bind programs (singletons like null or console,
        // bus drivers like pci, etc)
        if ((status = device_create("root", NULL, &root_ops, &root_driver, &dev)) < 0) {
            printf("devhost: cannot create root device: %d\n", status);
            return -1;
        }
        the_root_device = dev;
        if ((status = device_create("misc", NULL, &root_ops, &root_driver, &the_misc_device)) < 0) {
            printf("devhost: cannot create misc device: %d\n", status);
            return -1;
        }
        device_set_protocol(the_misc_device, MX_PROTOCOL_MISC_PARENT, NULL);
        as_root = true;
    } else if (!strncmp(argv[1], "pci=", 4)) {
        // The pci bus driver launches devhosts for pci devices.
        // Later we'll support other bus driver devhost launching.
        uint32_t index = strtoul(argv[1] + 4, NULL, 10);
        if ((status = devhost_create_pcidev(&dev, index)) < 0) {
            printf("devhost: cannot create pci device: %d\n", status);
            return -1;
        }
    } else if (!strcmp(argv[1], "soc")) {
        if (argc < 4) return -1;
        if ((status = device_create("soc", NULL, &root_ops, &root_driver, &dev)) < 0) {
            printf("devhost: cannot create SoC device: %d\n", status);
            return -1;
        }
        device_set_protocol(dev, MX_PROTOCOL_SOC, NULL);
        dev->props = calloc(2, sizeof(mx_device_prop_t));
        dev->props[0].id = BIND_SOC_VID;
        dev->props[0].value = strtoul(argv[2],NULL,10);
        dev->props[1].id = BIND_SOC_PID;
        dev->props[1].value = strtoul(argv[3],NULL,10);
        dev->prop_count=2;
    } else if (!strcmp(argv[1], "acpi")) {
        if ((status = device_create("acpi", NULL, &root_ops, &root_driver, &dev)) < 0) {
            printf("devhost: cannot create ACPI bus device: %d\n", status);
            return -1;
        }
        device_set_protocol(dev, MX_PROTOCOL_ACPI_BUS, NULL);
    } else {
        printf("devhost: unsupported mode: %s\n", argv[1]);
        return -1;
    }

    if ((status = devhost_device_add_root(dev)) < 0) {
        printf("devhost: cannot install root device: %d\n", status);
        return -1;
    }
    if ((status = devhost_connect(dev, hdevice, hrpc)) < 0) {
        printf("devhost: cannot connect root device: %d\n", status);
        return -1;
    }
    if (the_misc_device) {
        if ((status = device_add(the_misc_device, the_root_device)) < 0) {
            printf("devhost: cannot install misc device: %d\n", status);
            return -1;
        }
    }
    return 0;
}

extern driver_api_t devhost_api;

void devhost_init_drivers(bool as_root);

int main(int argc, char** argv) {
    int r;
    bool as_root = false;

    driver_api_init(&devhost_api);

    devhost_io_init();

    if ((r = devhost_init()) < 0) {
        return r;
    }

    if ((argc > 1) && (!strcmp(argv[1], "root"))) {
        as_root = true;

        mx_status_t status = devhost_launch_acpisvc(job_handle);
        if (status != NO_ERROR) {
            return 1;
        }

        // Ignore the return value of this; if it fails, it may just be that the
        // platform doesn't support initing PCIe via ACPI.  If the platform needed
        // it, it will fail later.
        devhost_init_pcie();
    }
    if ((r = devhost_cmdline(argc, argv)) < 0) {
        return r;
    }

    devhost_init_drivers(as_root);

    mxio_dispatcher_run(devhost_rio_dispatcher);
    printf("devhost: rio dispatcher exited?\n");
    return 0;
}

void devhost_launch_devhost(mx_device_t* parent, const char* name, uint32_t protocol_id,
                            const char* procname, int argc, char** argv) {
    mx_handle_t hdevice, hrpc;
    if (devhost_add_internal(parent, name, protocol_id, &hdevice, &hrpc) < 0) {
        return;
    }

    devmgr_launch_devhost(job_handle, procname, argc, argv, hdevice, hrpc);
}
