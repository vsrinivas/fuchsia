// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <ddk/protocol/device.h>
#include <fs-management/mount.h>
#include <gpt/gpt.h>
#include <magenta/device/block.h>
#include <magenta/device/console.h>
#include <magenta/device/devmgr.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/debug.h>
#include <mxio/util.h>
#include <mxio/watcher.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include "devmgr.h"

#define VC_COUNT 3

static mx_handle_t root_resource_handle;
static mx_handle_t root_job_handle;
static mx_handle_t svcs_job_handle;

static mx_handle_t application_launcher_child;
mx_handle_t application_launcher;

mx_handle_t get_root_resource(void) {
    return root_resource_handle;
}
mx_handle_t get_sysinfo_job_root(void) {
    mx_handle_t h;
    //TODO: limit to enumerate rights
    if (mx_handle_duplicate(root_job_handle, MX_RIGHT_SAME_RIGHTS, &h) < 0) {
        return MX_HANDLE_INVALID;
    } else {
        return h;
    }
}

#define VC_DEVICE "/dev/class/console/vc"

static bool switch_to_first_vc(void) {
    char* v = getenv("startup.keep-log-visible");
    if (!v) return true;
    // If this flag is disabled, meaning any of the following strcmps returns 0,
    // then we switch. Otherwise we stay on the kernel logs.
    return !strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "off");
}

static mx_status_t launch_minfs(int argc, const char** argv, mx_handle_t h) {
    devmgr_launch(svcs_job_handle, "minfs:/data", argc, argv, -1, h,
                  MX_HND_TYPE_USER0);
    return NO_ERROR;
}

static mx_status_t launch_fat(int argc, const char** argv, mx_handle_t h) {
    devmgr_launch(svcs_job_handle, "fatfs:/volume", argc, argv, -1, h,
                  MX_HND_TYPE_USER0);
    return NO_ERROR;
}

static mx_status_t block_device_added(int dirfd, const char* name, void* cookie) {
    printf("devmgr: new block device: /dev/class/block/%s\n", name);
    int fd;
    if ((fd = openat(dirfd, name, O_RDWR)) < 0) {
        return NO_ERROR;
    }

    disk_format_t df = detect_disk_format(fd);

    // TODO(smklein): Pass block devices by handle, not pathname, since
    // accessing devices by pathnames is inherently racy.
    switch (df) {
    case DISK_FORMAT_GPT: {
        printf("devmgr: /dev/class/block/%s: GPT?\n", name);
        // probe for partition table
        ioctl_device_bind(fd, "gpt", 4);
        close(fd);
        return NO_ERROR;
    }
    case DISK_FORMAT_MINFS: {
        close(fd);
        char devicepath[MXIO_MAX_FILENAME + 64];
        snprintf(devicepath, sizeof(devicepath), "/dev/class/block/%s", name);
        mount_options_t options;
        memcpy(&options, &default_mount_options, sizeof(mount_options_t));
        printf("devmgr: %s: minfs?\n", devicepath);
        mount(devicepath, "/data", df, &options, launch_minfs);
        return NO_ERROR;
    }
    case DISK_FORMAT_FAT: {
        // Use the GUID to avoid auto-mounting the EFI partition as writable
        uint8_t guid[GPT_GUID_LEN];
        ssize_t r = ioctl_block_get_type_guid(fd, guid, sizeof(guid));
        close(fd);
        bool efi = false;
        static const uint8_t guid_efi_part[GPT_GUID_LEN] = GUID_EFI_VALUE;
        if (r == GPT_GUID_LEN && !memcmp(guid, guid_efi_part, GPT_GUID_LEN)) {
            efi = true;
        }
        mount_options_t options;
        memcpy(&options, &default_mount_options, sizeof(mount_options_t));
        options.readonly = efi;
        char devicepath[MXIO_MAX_FILENAME + 64];
        snprintf(devicepath, sizeof(devicepath), "/dev/class/block/%s", name);
        static int fat_counter = 0;
        static int efi_counter = 0;
        char mountpath[MXIO_MAX_FILENAME + 64];
        if (efi) {
            snprintf(mountpath, sizeof(mountpath), "/volume/efi-%d", efi_counter++);
        } else {
            snprintf(mountpath, sizeof(mountpath), "/volume/fat-%d", fat_counter++);
        }
        mkdir(mountpath, 0755);
        printf("devmgr: %s: fatfs?\n", devicepath);
        mount(devicepath, mountpath, df, &options, launch_fat);
        return NO_ERROR;
    }
    default:
        close(fd);
        return NO_ERROR;
    }
}

static const char* argv_netsvc[] = { "/boot/bin/netsvc" };
static const char* argv_sh[] = { "/boot/bin/sh" };
static const char* argv_mxsh_autorun[] = { "/boot/bin/mxsh", "/boot/autorun", "/system/autorun" };
static const char* argv_appmgr[] = { "/system/bin/application_manager" };

void create_application_launcher_handles(void) {
    mx_channel_create(0, &application_launcher, &application_launcher_child);
}

int service_starter(void* arg) {
    if (getenv("netsvc.disable") == NULL) {
        // launch the network service
        devmgr_launch(svcs_job_handle, "netsvc", countof(argv_netsvc), argv_netsvc, -1, 0, 0);
    }

    devmgr_launch(svcs_job_handle, "mxsh:autorun", countof(argv_mxsh_autorun),
                  argv_mxsh_autorun, -1, 0, 0);

    if (application_launcher_child) {
        devmgr_launch(svcs_job_handle, "application-manager", countof(argv_appmgr), argv_appmgr,
                      -1, application_launcher_child,
                      MX_HND_INFO(MX_HND_TYPE_APPLICATION_LAUNCHER, 0));
        application_launcher_child = 0;
    }

    int dirfd;
    if ((dirfd = open("/dev/class/block", O_DIRECTORY|O_RDONLY)) >= 0) {
        mxio_watch_directory(dirfd, block_device_added, NULL);
    }
    close(dirfd);
    return 0;
}

#if !_MX_KERNEL_HAS_SHELL
static int console_starter(void* arg) {
    // if no kernel shell on serial uart, start a sh there
    printf("devmgr: shell startup\n");
    for (unsigned n = 0; n < 30; n++) {
        int fd;
        if ((fd = open("/dev/class/misc/console", O_RDWR)) >= 0) {
            devmgr_launch(svcs_job_handle, "sh:console", countof(argv_sh), argv_sh, fd, 0, 0);
            break;
        }
        mx_nanosleep(MX_MSEC(100));
    }
    return 0;
}

static void start_console_shell(void) {
    thrd_t t;
    if ((thrd_create_with_name(&t, console_starter, NULL, "console-starter")) == thrd_success) {
        thrd_detach(t);
    }
}
#else
static void start_console_shell(void) {}
#endif

static mx_status_t console_device_added(int dirfd, const char* name, void* cookie) {
    if (strcmp(name, "vc")) {
        return NO_ERROR;
    }

    // start some shells on vcs
    for (unsigned i = 0; i < VC_COUNT; i++) {
        int fd;
        if ((fd = openat(dirfd, name, O_RDWR)) >= 0) {
            if (i == 0 && switch_to_first_vc()) {
                ioctl_console_set_active_vc(fd);
            }
            devmgr_launch(svcs_job_handle, "sh:vc", countof(argv_sh), argv_sh, fd, 0, 0);
        }
    }

    // stop polling
    return 1;
}

int virtcon_starter(void* arg) {
    int dirfd;
    if ((dirfd = open("/dev/class/console", O_DIRECTORY|O_RDONLY)) >= 0) {
        mxio_watch_directory(dirfd, console_device_added, NULL);
    }
    close(dirfd);
    return 0;
}

int main(int argc, char** argv) {
    devmgr_io_init();

    root_resource_handle = mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_RESOURCE, 0));
    root_job_handle = mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_JOB, 0));

    printf("devmgr: main()\n");

    char** e = environ;
    while (*e) {
        printf("cmdline: %s\n", *e++);
    }

    devmgr_init(root_job_handle);
    devmgr_vfs_init();

    mx_status_t status = mx_job_create(root_job_handle, 0u, &svcs_job_handle);
    if (status < 0) {
        printf("unable to create service job\n");
    }

#if defined(__x86_64__) || defined(__aarch64__)
    if (!getenv("crashlogger.disable")) {
        static const char* argv_crashlogger[] = { "/boot/bin/crashlogger" };
        devmgr_launch(svcs_job_handle, "crashlogger", 1, argv_crashlogger, -1, 0, 0);
    }
#else
    // Until crashlogging exists, ensure we see load info
    // from the linker in the log
    putenv(strdup("LD_DEBUG=1"));
#endif

    start_console_shell();

    create_application_launcher_handles();

    thrd_t t;
    if ((thrd_create_with_name(&t, service_starter, NULL, "service-starter")) == thrd_success) {
        thrd_detach(t);
    }
    if (getenv("virtcon.disable") == NULL) {
        if ((thrd_create_with_name(&t, virtcon_starter, NULL,
                                   "virtcon-starter")) == thrd_success) {
            thrd_detach(t);
        }
    }

    devmgr_handle_messages();
    printf("devmgr: message handler returned?!\n");
    return 0;
}
