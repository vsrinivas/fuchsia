// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
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
#include "memfs-private.h"

#define VC_COUNT 3

static mx_handle_t root_resource_handle;
static mx_handle_t root_job_handle;
static mx_handle_t svcs_job_handle;

static mx_handle_t application_launcher_child;
mx_handle_t application_launcher;

// Sent to netsvc
static mx_handle_t netsvc_ipc = 0;
// Sent to init once /system is mounted
static mx_handle_t netsvc_ipc_child = 0;

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

static mx_status_t launch_minfs(int argc, const char** argv, mx_handle_t* hnd,
                                uint32_t* ids, size_t len) {
    devmgr_launch(svcs_job_handle, "minfs:/data", argc, argv, NULL, -1,
                  hnd, ids, len);
    return NO_ERROR;
}

static mx_status_t launch_fat(int argc, const char** argv, mx_handle_t* hnd,
                              uint32_t* ids, size_t len) {
    devmgr_launch(svcs_job_handle, "fatfs:/volume", argc, argv, NULL, -1,
                  hnd, ids, len);
    return NO_ERROR;
}

static bool data_mounted = false;

/*
 * Attempt to mount the device pointed to be the file descriptor at a known
 * location.
 * Returns ERR_ALREADY_BOUND if the device could be mounted, but something
 * is already mounted at that location. Returns ERR_INVALID_ARGS if the
 * GUID of the device does not match a known valid one. Returns NO_ERROR if an
 * attempt to mount is made, without checking mount success.
 */
static int mount_minfs(int fd, mount_options_t* options) {
    uint8_t type_guid[GPT_GUID_LEN];
    static const uint8_t sys_guid[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
    static const uint8_t data_guid[GPT_GUID_LEN] = GUID_DATA_VALUE;

    // initialize our data for this run
    const char* path = NULL;
    ssize_t read_sz = ioctl_block_get_type_guid(fd, type_guid,
                                                sizeof(type_guid));

    // check if this partition matches any special type GUID
    if (read_sz == GPT_GUID_LEN) {
        if (!memcmp(type_guid, sys_guid, GPT_GUID_LEN)) {
            if (secondary_bootfs_ready()) {
                return ERR_ALREADY_BOUND;
            }
            memfs_create_directory("/system", 0);
            path = "/system";
            options->readonly = true;
            devmgr_start_system_init(NULL);
        } else if (!memcmp(type_guid, data_guid, GPT_GUID_LEN)) {
            if (data_mounted) {
                return ERR_ALREADY_BOUND;
            }
            data_mounted = true;
            path = "/data";
        }
    }

    // if the path is set, this partition has a known type GUID
    if (path != NULL) {
        mount(fd, path, DISK_FORMAT_MINFS, options, launch_minfs);
        return NO_ERROR;
    }

    return ERR_INVALID_ARGS;
}

static mx_status_t block_device_added(int dirfd, const char* name, void* cookie) {
    printf("devmgr: new block device: /dev/class/block/%s\n", name);
    int fd;
    if ((fd = openat(dirfd, name, O_RDWR)) < 0) {
        return NO_ERROR;
    }

    disk_format_t df = detect_disk_format(fd);

    switch (df) {
    case DISK_FORMAT_GPT: {
        printf("devmgr: /dev/class/block/%s: GPT?\n", name);
        // probe for partition table
        ioctl_device_bind(fd, "gpt", 4);
        close(fd);
        return NO_ERROR;
    }
    case DISK_FORMAT_MBR: {
        printf("devmgr: /dev/class/block/%s: MBR?\n", name);
        // probe for partition table
        ioctl_device_bind(fd, "mbr", 4);
        close(fd);
        return NO_ERROR;
    }
    case DISK_FORMAT_MINFS: {
        mount_options_t options;
        memcpy(&options, &default_mount_options, sizeof(mount_options_t));
        printf("devmgr: minfs\n");
        if (mount_minfs(fd, &options) != NO_ERROR) {
            close(fd);
        }
        return NO_ERROR;
    }
    case DISK_FORMAT_FAT: {
        // Use the GUID to avoid auto-mounting the EFI partition as writable
        uint8_t guid[GPT_GUID_LEN];
        ssize_t r = ioctl_block_get_type_guid(fd, guid, sizeof(guid));
        bool efi = false;
        static const uint8_t guid_efi_part[GPT_GUID_LEN] = GUID_EFI_VALUE;
        if (r == GPT_GUID_LEN && !memcmp(guid, guid_efi_part, GPT_GUID_LEN)) {
            efi = true;
        }
        mount_options_t options;
        memcpy(&options, &default_mount_options, sizeof(mount_options_t));
        options.readonly = efi;
        static int fat_counter = 0;
        static int efi_counter = 0;
        char mountpath[MXIO_MAX_FILENAME + 64];
        if (efi) {
            snprintf(mountpath, sizeof(mountpath), "/volume/efi-%d", efi_counter++);
        } else {
            snprintf(mountpath, sizeof(mountpath), "/volume/fat-%d", fat_counter++);
        }
        mkdir(mountpath, 0755);
        printf("devmgr: fatfs\n");
        mount(fd, mountpath, df, &options, launch_fat);
        return NO_ERROR;
    }
    default:
        close(fd);
        return NO_ERROR;
    }
}

static const char* argv_netsvc[] = { "/boot/bin/netsvc" };
static const char* argv_sh[] = { "/boot/bin/sh" };
static const char* argv_autorun0[] = { "/boot/bin/sh", "/boot/autorun" };
static const char* argv_init[] = { "/system/bin/init" };

void create_application_launcher_handles(void) {
    mx_channel_create(0, &application_launcher, &application_launcher_child);
}

void create_netsvc_ipc_handles(void) {
    mx_channel_create(0, &netsvc_ipc, &netsvc_ipc_child);
}

int devmgr_start_system_init(void* arg) {
    static bool init_started = false;
    static mtx_t lock = MTX_INIT;
    mtx_lock(&lock);
    struct stat s;
    if (!init_started && stat(argv_init[0], &s) == 0) {
        unsigned int init_hnd_count = 0;
        mx_handle_t init_hnds[2] = {};
        uint32_t init_ids[2] = {};
        if (application_launcher_child) {
            assert(init_hnd_count < countof(init_hnds));
            init_hnds[init_hnd_count] = application_launcher_child;
            init_ids[init_hnd_count] = MX_HND_INFO(MX_HND_TYPE_APPLICATION_LAUNCHER, 0);
            init_hnd_count++;
            application_launcher_child = 0;
        }
        if (netsvc_ipc_child) {
            assert(init_hnd_count < countof(init_hnds));
            init_hnds[init_hnd_count] = netsvc_ipc_child;
            init_ids[init_hnd_count] = MX_HND_INFO(MX_HND_TYPE_USER0, 0);
            init_hnd_count++;
            netsvc_ipc_child = 0;
        }
        devmgr_launch(svcs_job_handle, "init", countof(argv_init),
                argv_init, NULL, -1, init_hnds, init_ids, init_hnd_count);
        init_started = true;
    }
    mtx_unlock(&lock);
    return 0;
}

int service_starter(void* arg) {
    if (getenv("netsvc.disable") == NULL) {
        // launch the network service
        uint32_t id = MX_HND_INFO(MX_HND_TYPE_USER0, 0);
        devmgr_launch(svcs_job_handle, "netsvc",
                countof(argv_netsvc), argv_netsvc, NULL, -1,
                &netsvc_ipc, &id, netsvc_ipc != MX_HANDLE_INVALID ? 1 : 0);
    }

    devmgr_launch(svcs_job_handle, "sh:autorun0", countof(argv_autorun0),
                  argv_autorun0, NULL, -1, NULL, NULL, 0);

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

    // If we got a TERM environment variable (aka a TERM=... argument on
    // the kernel command line), pass this down.
    const char* term = getenv("TERM");
    if (term != NULL)
        term -= sizeof("TERM=") - 1;

    for (unsigned n = 0; n < 30; n++) {
        int fd;
        if ((fd = open("/dev/class/misc/console", O_RDWR)) >= 0) {
            devmgr_launch(svcs_job_handle, "sh:console",
                          countof(argv_sh), argv_sh, term, fd, NULL, NULL, 0);
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
            devmgr_launch(svcs_job_handle, "sh:vc",
                          countof(argv_sh), argv_sh, NULL, fd, NULL, NULL, 0);
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
    root_job_handle = mx_job_default();

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
        devmgr_launch(svcs_job_handle, "crashlogger",
                      1, argv_crashlogger, NULL, -1, NULL, NULL, 0);
    }
#else
    // Until crashlogging exists, ensure we see load info
    // from the linker in the log
    putenv(strdup("LD_DEBUG=1"));
#endif

    start_console_shell();

    create_application_launcher_handles();
    create_netsvc_ipc_handles();

    if (secondary_bootfs_ready()) {
        devmgr_start_system_init(NULL);
    }

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
