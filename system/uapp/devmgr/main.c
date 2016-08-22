// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls-ddk.h>
#include <mxio/debug.h>
#include <mxio/util.h>
#include <runtime/thread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "devmgr.h"

#define VC_COUNT 3

void devmgr_io_init(void) {
    // setup stdout
    uint32_t flags = devmgr_is_remote ? MX_LOG_FLAG_DEVICE : MX_LOG_FLAG_DEVMGR;
    mx_handle_t h;
    if ((h = mx_log_create(flags)) < 0) {
        return;
    }
    mxio_t* logger;
    if ((logger = mxio_logger_create(h)) == NULL) {
        return;
    }
    close(1);
    mxio_bind_to_fd(logger, 1, 0);
}

int devicehost(int argc, char** argv) {
    devhost_handle = mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_USER1, 0));
    if (devhost_handle <= 0) {
        printf("devhost: no rpc handle?!\n");
        return -1;
    }
    if (argc != 3) {
        return -1;
    }
    if (!strncmp(argv[1], "pci=", 4)) {
        uint32_t index = strtoul(argv[1] + 4, NULL, 10);

        printf("devhost: pci host %d\n", index);
        devmgr_init(true);
        mx_device_t* pcidev;
        if (devmgr_create_pcidev(&pcidev, index)) {
            printf("devhost: cannot create pci device\n");
            return -1;
        }
        device_add(pcidev, devmgr_device_root());
        devmgr_init_builtin_drivers();
        devmgr_handle_messages();
    }
    printf("devhost: exiting\n");
    return 0;
}

#define VC_DEVICE "/dev/class/console/vc"

#if !LIBDRIVER
int console_starter(void* arg) {
#if !_MX_KERNEL_HAS_SHELL
    // if no kernel shell on serial uart, start a mxsh there
    printf("devmgr: shell startup\n");
    devmgr_launch("mxsh:console", "/boot/bin/mxsh", NULL, "/dev/console");
#endif

    devmgr_launch("netsvc", "/boot/bin/netsvc", NULL, "/dev/console");

    devmgr_launch("mxsh:autorun", "/boot/bin/mxsh", "/boot/autorun", NULL);

    printf("devmgr: vc startup\n");
    // wait for vc server ready
    int fd;
    while ((fd = open(VC_DEVICE, O_RDWR)) < 0) {
        mx_nanosleep(100000000ULL);
    }
    close(fd);

    // start a couple vc's
    for (unsigned i = 0; i < VC_COUNT; i++) {
        char pname[32];
        snprintf(pname, sizeof(pname), "mxsh:vc%u", i);
        devmgr_launch(pname, "/boot/bin/mxsh", NULL, VC_DEVICE);
    }
    return 0;
}
#endif

int main(int argc, char** argv) {
    devmgr_io_init();
    if (argc > 1) {
        return devicehost(argc, argv);
    }

#if LIBDRIVER
    printf("device driver - not a standalone executable\n");
#else
    printf("devmgr: main()\n");

    char** e = environ;
    while (*e) {
        printf("cmdline: %s\n", *e++);
    }

    devmgr_init(false);
    devmgr_vfs_init();

#if !defined(__x86_64__)
    // Until crashlogging exists, ensure we see load info
    // from the linker in the log
    putenv(strdup("LD_DEBUG=1"));
#else
    char* disable_crashlogger = getenv("crashlogger.disable");
    if (!disable_crashlogger)
        devmgr_launch("crashlogger", "/boot/bin/crashlogger", NULL, NULL);
#endif

    printf("devmgr: load drivers\n");
    devmgr_init_builtin_drivers();

    mxr_thread_t* t;
    if ((mxr_thread_create(console_starter, NULL, "console-starter", &t)) == 0) {
        mxr_thread_detach(t);
    }

    devmgr_handle_messages();
    printf("devmgr: message handler returned?!\n");
#endif
    return 0;
}
