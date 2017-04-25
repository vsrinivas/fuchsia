// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"
#include "devhost.h"
#include "devmgr.h"

#include <stdio.h>

#include <launchpad/launchpad.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>


#if DEVMGR
mx_handle_t vfs_create_global_root_handle(void);
#define LOG_FLAGS MX_LOG_FLAG_DEVMGR
#else
#define LOG_FLAGS MX_LOG_FLAG_DEVICE
#endif

void devmgr_io_init(void) {
    // setup stdout
    mx_handle_t h;
    if (mx_log_create(LOG_FLAGS, &h) < 0) {
        return;
    }
    mxio_t* logger;
    if ((logger = mxio_logger_create(h)) == NULL) {
        return;
    }
    close(1);
    mxio_bind_to_fd(logger, 1, 0);
}

extern mx_handle_t application_launcher;

void devmgr_launch_devhost(mx_handle_t job,
                           const char* name, int argc, char** argv,
                           mx_handle_t hdevice, mx_handle_t hrpc) {

    launchpad_t* lp;
    launchpad_create(job, name, &lp);
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, (const char* const*) argv);

    launchpad_add_handle(lp, hdevice, PA_HND(PA_USER0, ID_HDEVICE));
    launchpad_add_handle(lp, hrpc, PA_HND(PA_USER0, ID_HRPC));

    mx_handle_t h;
    mx_handle_duplicate(get_root_resource(), MX_RIGHT_SAME_RIGHTS, &h);
    launchpad_add_handle(lp, h, PA_HND(PA_RESOURCE, 0));

#if DEVMGR
    launchpad_clone(lp, LP_CLONE_ENVIRON);
    launchpad_add_handle(lp, vfs_create_global_root_handle(),
                         PA_HND(PA_MXIO_ROOT, 0));
    if (application_launcher) {
        launchpad_add_handle(lp, application_launcher,
                             PA_HND(PA_USER0, ID_HLAUNCHER));
    }
    if ((h = get_service_root()) != MX_HANDLE_INVALID) {
        launchpad_add_handle(lp, h, PA_SERVICE_ROOT);
    }
#else
    launchpad_clone(lp, LP_CLONE_ENVIRON | LP_CLONE_MXIO_ROOT);
    if ((h = devhost_acpi_clone()) > 0) {
        launchpad_add_handle(lp, h, PA_HND(PA_USER0, ID_HACPI));
    }
#endif

    //TODO: maybe migrate this to using the default job mechanism
    launchpad_add_handle(lp, get_sysinfo_job_root(),
                         PA_HND(PA_USER0, ID_HJOBROOT));


    printf("devmgr: launch: %s %s %s\n", name, argv[0], argv[1]);
    const char* errmsg;
    mx_status_t status = launchpad_go(lp, NULL, &errmsg);
    if (status < 0) {
        printf("devmgr: launch %s %s %s failed: %d: %s\n",
               name, argv[0], argv[1], status, errmsg);
    }
}
