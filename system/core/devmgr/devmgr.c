// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <ddk/protocol/device.h>
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

static const uint8_t minfs_magic[16] = {
    0x21, 0x4d, 0x69, 0x6e, 0x46, 0x53, 0x21, 0x00,
    0x04, 0xd3, 0xd3, 0xd3, 0xd3, 0x00, 0x50, 0x38,
};

static const uint8_t gpt_magic[16] = {
    0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54,
    0x00, 0x00, 0x01, 0x00, 0x5c, 0x00, 0x00, 0x00,
};

static bool switch_to_first_vc(void) {
    char* v = getenv("startup.keep-log-visible");
    return v ? strcmp(v, "true") != 0 : true;
}

// Mount a handle to a remote filesystem on a directory.
// Any future requests made through the path at "where" will be transmitted to
// the handle returned from this function.
static mx_status_t mount_remote_handle(const char* where, mx_handle_t* h) {
    int fd;
    if ((fd = open(where, O_DIRECTORY | O_RDWR)) < 0) {
        return MX_HANDLE_INVALID;
    }
    if (ioctl_devmgr_mount_fs(fd, h) != sizeof(mx_handle_t)) {
        close(fd);
        return MX_HANDLE_INVALID;
    }
    close(fd);
    return NO_ERROR;
}

static void launch_minfs(const char* device_name) {
    mx_handle_t h;
    mx_status_t status = mount_remote_handle("/data", &h);
    if (status != NO_ERROR) {
        printf("devmgr: Failed to mount remote handle handle at '/data'\n");
        return;
    }
    char device_path[MXIO_MAX_FILENAME + 64];
    snprintf(device_path, sizeof(device_path), "/dev/class/block/%s", device_name);
    const char* argv[] = { "/boot/bin/minfs", device_path, "mount", "/data" };
    printf("devmgr: /dev/class/block/%s: minfs?\n", device_name);
    devmgr_launch(svcs_job_handle, "minfs:/data", sizeof(argv)/sizeof(argv[0]),
                  argv, -1, h, MX_HND_TYPE_USER0);
}

static void launch_fat(const char* device_name) {
    char device_path_arg[MXIO_MAX_FILENAME + 64];
    snprintf(device_path_arg, sizeof(device_path_arg),
             "/dev/class/block/%s", device_name);
    int fd = open(device_path_arg, O_RDWR);
    if (fd < 0) {
        printf("Could not open block device: %s\n", device_path_arg);
        return;
    }
    // Use the GUID to avoid auto-mounting the EFI partition as writable
    uint8_t guid[GPT_GUID_LEN];
    ssize_t r = ioctl_block_get_type_guid(fd, guid, sizeof(guid));
    bool efi = false;
    static const uint8_t guid_efi_part[GPT_GUID_LEN] = GUID_EFI_VALUE;
    if (r == GPT_GUID_LEN && !memcmp(guid, guid_efi_part, GPT_GUID_LEN)) {
        efi = true;
    }
    close(fd);
    char readonly_arg[64];
    snprintf(readonly_arg, sizeof(readonly_arg), "-readonly=%s", efi ? "true" : "false");

    snprintf(device_path_arg, sizeof(device_path_arg),
             "-devicepath=/dev/class/block/%s", device_name);

    static int fat_counter = 0;
    static int efi_counter = 0;
    char mount_path[MXIO_MAX_FILENAME + 64];
    if (efi) {
        snprintf(mount_path, sizeof(mount_path), "/volume/efi-%d", efi_counter++);
    } else {
        snprintf(mount_path, sizeof(mount_path), "/volume/fat-%d", fat_counter++);
    }
    mkdir(mount_path, 0755);
    mx_handle_t h;
    mx_status_t status = mount_remote_handle(mount_path, &h);
    if (status != NO_ERROR) {
        printf("devmgr: Failed to mount remote handle handle at '%s'\n", mount_path);
        return;
    }

    const char* argv[] = {
        "/system/bin/thinfs",
        device_path_arg,
        readonly_arg,
        "mount",
    };
    printf("devmgr: /dev/class/block/%s: fatfs?\n", device_name);
    devmgr_launch(svcs_job_handle, "fatfs:/volume",
                  sizeof(argv)/sizeof(argv[0]), argv, -1, h, MX_HND_TYPE_USER0);
}

static mx_status_t block_device_added(int dirfd, const char* name, void* cookie) {
    uint8_t data[4096];
    printf("devmgr: new block device: /dev/class/block/%s\n", name);
    int fd;
    if ((fd = openat(dirfd, name, O_RDWR)) < 0) {
        return NO_ERROR;
    }

    if (read(fd, data, sizeof(data)) != sizeof(data)) {
        close(fd);
        printf("devmgr: cannot read: /dev/class/block/%s\n", name);
        return NO_ERROR;
    }

    // TODO(smklein): Pass block devices by handle, not pathname, since
    // accessing devices by pathnames is inherently racy.
    if (!memcmp(data + 0x200, gpt_magic, sizeof(gpt_magic))) {
        printf("devmgr: /dev/class/block/%s: GPT?\n", name);
        // probe for partition table
        ioctl_device_bind(fd, "gpt", 4);
        close(fd);
    } else if(!memcmp(data, minfs_magic, sizeof(minfs_magic))) {
        close(fd);
        launch_minfs(name);
    } else if ((data[510] == 0x55 && data[511] == 0xAA) && (data[38] == 0x29 ||
                                                            data[66] == 0x29)) {
        // 0x55AA are always placed at offset 510 and 511 for FAT filesystems.
        // 0x29 is the Boot Signature, but it is placed at either offset 38 or
        // 66 (depending on FAT type).
        close(fd);
        launch_fat(name);
    } else {
        close(fd);
    }

    return NO_ERROR;
}

static const char* argv_netsvc[] = { "/boot/bin/netsvc" };
static const char* argv_mxsh[] = { "/boot/bin/mxsh" };
static const char* argv_mxsh_autorun[] = { "/boot/bin/mxsh", "/boot/autorun" };
static const char* argv_appmgr[] = { "/system/bin/application_manager" };

void create_application_launcher_handles(void) {
    mx_channel_create(0, &application_launcher, &application_launcher_child);
}

int service_starter(void* arg) {
    if (getenv("netsvc.disable") == NULL) {
        // launch the network service
        devmgr_launch(svcs_job_handle, "netsvc", 1, argv_netsvc, -1, 0, 0);
    }

    devmgr_launch(svcs_job_handle, "mxsh:autorun", 2, argv_mxsh_autorun, -1, 0, 0);

    if (application_launcher_child) {
        devmgr_launch(svcs_job_handle, "application-manager", 1, argv_appmgr, -1,
                      application_launcher_child,
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
    // if no kernel shell on serial uart, start a mxsh there
    printf("devmgr: shell startup\n");
    for (unsigned n = 0; n < 30; n++) {
        int fd;
        if ((fd = open("/dev/class/misc/console", O_RDWR)) >= 0) {
            devmgr_launch(svcs_job_handle, "mxsh:console", 1, argv_mxsh, fd, 0, 0);
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
            devmgr_launch(svcs_job_handle, "mxsh:vc", 1, argv_mxsh, fd, 0, 0);
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
