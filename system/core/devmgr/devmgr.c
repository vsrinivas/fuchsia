// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <magenta/device/device.h>
#include <fs-management/mount.h>
#include <gpt/gpt.h>
#include <launchpad/launchpad.h>
#include <magenta/device/block.h>
#include <magenta/device/console.h>
#include <magenta/device/vfs.h>
#include <magenta/dlfcn.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/status.h>
#include <mxio/debug.h>
#include <mxio/loader-service.h>
#include <mxio/watcher.h>
#include <mxio/util.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include "devmgr.h"
#include "memfs-private.h"

static mx_handle_t svc_root_handle;
static mx_handle_t svc_request_handle;

mx_handle_t get_service_root(void) {
    return mxio_service_clone(svc_root_handle);
}

static mx_handle_t root_resource_handle;
static mx_handle_t root_job_handle;
static mx_handle_t svcs_job_handle;
static mx_handle_t fuchsia_job_handle;

mx_handle_t virtcon_open;

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

static mx_status_t launch_blobstore(int argc, const char** argv, mx_handle_t* hnd,
                                    uint32_t* ids, size_t len) {
    return devmgr_launch(svcs_job_handle, "blobstore:/blobstore", argc, argv, NULL, -1,
                         hnd, ids, len);
}

static mx_status_t launch_minfs(int argc, const char** argv, mx_handle_t* hnd,
                                uint32_t* ids, size_t len) {
    return devmgr_launch(svcs_job_handle, "minfs:/data", argc, argv, NULL, -1,
                         hnd, ids, len);
}

static mx_status_t launch_fat(int argc, const char** argv, mx_handle_t* hnd,
                              uint32_t* ids, size_t len) {
    return devmgr_launch(svcs_job_handle, "fatfs:/volume", argc, argv, NULL, -1,
                         hnd, ids, len);
}

static bool data_mounted = false;

/*
 * Attempt to mount the device pointed to be the file descriptor at a known
 * location.
 * Returns MX_ERR_ALREADY_BOUND if the device could be mounted, but something
 * is already mounted at that location. Returns MX_ERR_INVALID_ARGS if the
 * GUID of the device does not match a known valid one. Returns MX_OK if an
 * attempt to mount is made, without checking mount success.
 */
static mx_status_t mount_minfs(int fd, mount_options_t* options) {
    uint8_t type_guid[GPT_GUID_LEN];
    static const uint8_t sys_guid[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
    static const uint8_t data_guid[GPT_GUID_LEN] = GUID_DATA_VALUE;

    // initialize our data for this run
    ssize_t read_sz = ioctl_block_get_type_guid(fd, type_guid,
                                                sizeof(type_guid));

    // check if this partition matches any special type GUID
    if (read_sz == GPT_GUID_LEN) {
        if (!memcmp(type_guid, sys_guid, GPT_GUID_LEN)) {
            if (secondary_bootfs_ready()) {
                return MX_ERR_ALREADY_BOUND;
            }

            options->readonly = true;
            options->wait_until_ready = true;
            options->create_mountpoint = true;

            mx_status_t st = mount(fd, "/system", DISK_FORMAT_MINFS, options, launch_minfs);
            if (st != MX_OK) {
                printf("devmgr: failed to mount /system, retcode = %d\n", st);
            } else {
                devmgr_start_appmgr(NULL);
            }

            return MX_OK;
        } else if (!memcmp(type_guid, data_guid, GPT_GUID_LEN)) {
            if (data_mounted) {
                return MX_ERR_ALREADY_BOUND;
            }
            data_mounted = true;
            options->wait_until_ready = true;

            mx_status_t st = mount(fd, "/data", DISK_FORMAT_MINFS, options, launch_minfs);
            if (st != MX_OK) {
                printf("devmgr: failed to mount /data, retcode = %d\n", st);
            }

            return MX_OK;
        }
    }

    return MX_ERR_INVALID_ARGS;
}

#define GPT_DRIVER_LIB "/boot/driver/gpt.so"
#define MBR_DRIVER_LIB "/boot/driver/mbr.so"
#define STRLEN(s) sizeof(s)/sizeof((s)[0])

static mx_status_t block_device_added(int dirfd, int event, const char* name, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        printf("devmgr: block watch waiting...\n");
        return MX_OK;
    }

    printf("devmgr: new block device: /dev/class/block/%s\n", name);
    int fd;
    if ((fd = openat(dirfd, name, O_RDWR)) < 0) {
        return MX_OK;
    }

    disk_format_t df = detect_disk_format(fd);

    switch (df) {
    case DISK_FORMAT_GPT: {
        printf("devmgr: /dev/class/block/%s: GPT?\n", name);
        // probe for partition table
        ioctl_device_bind(fd, GPT_DRIVER_LIB, STRLEN(GPT_DRIVER_LIB));
        close(fd);
        return MX_OK;
    }
    case DISK_FORMAT_MBR: {
        printf("devmgr: /dev/class/block/%s: MBR?\n", name);
        // probe for partition table
        ioctl_device_bind(fd, MBR_DRIVER_LIB, STRLEN(MBR_DRIVER_LIB));
        close(fd);
        return MX_OK;
    }
    case DISK_FORMAT_BLOBFS: {
        mount_options_t options = default_mount_options;
        options.create_mountpoint = true;
        mount(fd, "/blobstore", DISK_FORMAT_BLOBFS, &options, launch_blobstore);
        return MX_OK;
    }
    case DISK_FORMAT_MINFS: {
        mount_options_t options = default_mount_options;
        options.wait_until_ready = false;
        printf("devmgr: minfs\n");
        if (mount_minfs(fd, &options) != MX_OK) {
            close(fd);
        }
        return MX_OK;
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
        mount_options_t options = default_mount_options;
        options.create_mountpoint = true;
        options.readonly = efi;
        static int fat_counter = 0;
        static int efi_counter = 0;
        char mountpath[MXIO_MAX_FILENAME + 64];
        if (efi) {
            snprintf(mountpath, sizeof(mountpath), "/volume/efi-%d", efi_counter++);
        } else {
            snprintf(mountpath, sizeof(mountpath), "/volume/fat-%d", fat_counter++);
        }
        options.wait_until_ready = false;
        printf("devmgr: fatfs\n");
        mount(fd, mountpath, df, &options, launch_fat);
        return MX_OK;
    }
    default:
        close(fd);
        return MX_OK;
    }
}

static const char* argv_sh[] = { "/boot/bin/sh" };
static const char* argv_autorun0[] = { "/boot/bin/sh", "/boot/autorun" };
static const char* argv_appmgr[] = { "/system/bin/appmgr" };

void do_autorun(const char* name, const char* env) {
    char* bin = getenv(env);
    if (bin) {
        printf("devmgr: %s: starting %s...\n", env, bin);
        devmgr_launch(svcs_job_handle, name,
                      1, (const char* const*) &bin,
                      NULL, -1, NULL, NULL, 0);
    }
}

int devmgr_start_appmgr(void* arg) {
    static bool appmgr_started = false;
    static bool autorun_started = false;
    static mtx_t lock = MTX_INIT;

    // we're starting the appmgr because /system is present
    // so we also signal the device coordinator that those
    // drivers are now loadable
    load_system_drivers();

    mtx_lock(&lock);
    struct stat s;
    if (!appmgr_started && stat(argv_appmgr[0], &s) == 0) {
        unsigned int appmgr_hnd_count = 0;
        mx_handle_t appmgr_hnds[2] = {};
        uint32_t appmgr_ids[2] = {};
        if (svc_request_handle) {
            assert(appmgr_hnd_count < countof(appmgr_hnds));
            appmgr_hnds[appmgr_hnd_count] = svc_request_handle;
            appmgr_ids[appmgr_hnd_count] = PA_SERVICE_REQUEST;
            appmgr_hnd_count++;
            svc_request_handle = 0;
        }
        devmgr_launch(fuchsia_job_handle, "appmgr", countof(argv_appmgr),
                      argv_appmgr, NULL, -1, appmgr_hnds, appmgr_ids,
                      appmgr_hnd_count);
        appmgr_started = true;
    }
    if (!autorun_started) {
        do_autorun("autorun:system", "magenta.autorun.system");
        autorun_started = true;
    }
    mtx_unlock(&lock);
    return 0;
}

int service_starter(void* arg) {
    // create a directory for sevice rendezvous
    mkdir("/svc", 0755);

    if (getenv("virtcon.disable") == NULL) {
        // pass virtcon.* options along
        const char* envp[16];
        unsigned envc = 0;
        char** e = environ;
        while (*e && (envc < countof(envp))) {
            if (!strncmp(*e, "virtcon.", 8)) {
                envp[envc++] = *e;
            }
            e++;
        }
        envp[envc] = NULL;

        uint32_t type = PA_HND(PA_USER0, 0);
        mx_handle_t h = MX_HANDLE_INVALID;
        mx_channel_create(0, &h, &virtcon_open);
        const char* args[] = { "/boot/bin/virtual-console" };
        devmgr_launch(svcs_job_handle, "virtual-console", 1, args, envp, -1,
                      &h, &type, (h == MX_HANDLE_INVALID) ? 0 : 1);
    }

    if (getenv("netsvc.disable") == NULL) {
        const char* args[] = { "/boot/bin/netsvc", NULL, NULL };
        int argc = 1;

        if (getenv("netsvc.netboot")) {
            args[argc++] = "--netboot";
        }

        const char* nodename = getenv("magenta.nodename");
        if (nodename) {
            args[argc++] = nodename;
        }

        devmgr_launch(svcs_job_handle, "netsvc", argc, args,
                      NULL, -1, NULL, NULL, 0);
    }

    do_autorun("autorun:boot", "magenta.autorun.boot");
    struct stat s;
    if (stat(argv_autorun0[1], &s) == 0) {
        printf("devmgr: starting /boot/autorun ...\n");
        devmgr_launch(svcs_job_handle, "sh:autorun0",
                      countof(argv_autorun0), argv_autorun0,
                      NULL, -1, NULL, NULL, 0);
    }

    int dirfd;
    if ((dirfd = open("/dev/class/block", O_DIRECTORY|O_RDONLY)) >= 0) {
        mxio_watch_directory(dirfd, block_device_added, MX_TIME_INFINITE, NULL);
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

    const char* envp[] = { term ? term : NULL, NULL, };
    for (unsigned n = 0; n < 30; n++) {
        int fd;
        if ((fd = open("/dev/misc/console", O_RDWR)) >= 0) {
            devmgr_launch(svcs_job_handle, "sh:console",
                          countof(argv_sh), argv_sh, envp, fd, NULL, NULL, 0);
            break;
        }
        mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
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

static void fetch_vdsos(void) {
    for (uint_fast16_t i = 0; true; ++i) {
        mx_handle_t vdso_vmo = mx_get_startup_handle(PA_HND(PA_VMO_VDSO, i));
        if (vdso_vmo == MX_HANDLE_INVALID)
            break;
        if (i == 0) {
            // The first one is the default vDSO.  Since we've stolen
            // the startup handle, launchpad won't find it on its own.
            // So point launchpad at it.
            launchpad_set_vdso_vmo(vdso_vmo);
        }

        // The vDSO VMOs have names like "vdso/default", so those
        // become VMO files at "/boot/vdso/default".
        char name[MX_MAX_NAME_LEN];
        size_t size;
        mx_status_t status = mx_object_get_property(vdso_vmo, MX_PROP_NAME,
                                                    name, sizeof(name));
        if (status != MX_OK) {
            printf("devmgr: mx_object_get_property on PA_VMO_VDSO %u: %s\n",
                   i, mx_status_get_string(status));
            continue;
        }
        status = mx_vmo_get_size(vdso_vmo, &size);
        if (status != MX_OK) {
            printf("devmgr: mx_vmo_get_size on PA_VMO_VDSO %u: %s\n",
                   i, mx_status_get_string(status));
            continue;
        }
        status = bootfs_add_file(name, vdso_vmo, 0, size);
        if (status != MX_OK) {
            printf("devmgr: failed to add PA_VMO_VDSO %u to filesystem: %s\n",
                   i, mx_status_get_string(status));
        }
    }
}

int main(int argc, char** argv) {
    // Close the loader-service channel so the service can go away.
    // We won't use it any more (no dlopen calls in this process).
    mx_handle_t loader_svc = dl_set_loader_service(MX_HANDLE_INVALID);
    mx_handle_close(loader_svc);

    // Ensure that devmgr doesn't try to connect to the global
    // loader sevice (as this leads to deadlocks in devhost v2)
    mxio_force_local_loader_service();

    devmgr_io_init();

    root_resource_handle = mx_get_startup_handle(PA_HND(PA_RESOURCE, 0));
    root_job_handle = mx_job_default();

    printf("devmgr: main()\n");

    char** e = environ;
    while (*e) {
        printf("cmdline: %s\n", *e++);
    }

    devmgr_init(root_job_handle);
    devmgr_vfs_init();

    mx_object_set_property(root_job_handle, MX_PROP_NAME, "root", 4);

    fetch_vdsos();

    mx_status_t status = mx_job_create(root_job_handle, 0u, &svcs_job_handle);
    if (status < 0) {
        printf("unable to create service job\n");
    }
    mx_object_set_property(svcs_job_handle, MX_PROP_NAME, "magenta-services", 16);

    status = mx_job_create(root_job_handle, 0u, &fuchsia_job_handle);
    if (status < 0) {
        printf("unable to create service job\n");
    }
    mx_object_set_property(fuchsia_job_handle, MX_PROP_NAME, "fuchsia", 7);

    // Features like Intel Processor Trace need a dump of ld.so activity.
    // The output has a specific format, and will eventually be recorded
    // via a specific mechanism (magenta tracing support), so we use a specific
    // env var (and don't, for example, piggyback on LD_DEBUG).
    // We enable this pretty early so that we get a trace of as many processes
    // as possible.
    if (getenv(LDSO_TRACE_CMDLINE)) {
        // This takes care of places that clone our environment.
        putenv(strdup(LDSO_TRACE_ENV));
        // There is still devmgr_launch() which does not clone our enviroment.
        // It has its own check.
    }

    if (!getenv("crashlogger.disable")) {
        static const char* argv_crashlogger[] = {
            "/boot/bin/crashlogger",
            NULL,  // room for -pton
        };
        const char* crashlogger_pt = getenv("crashlogger.pt");
        int argc_crashlogger = 1;
        if (crashlogger_pt && strcmp(crashlogger_pt, "true") == 0) {
            // /dev/misc/intel-pt may not be available yet, so we can't
            // actually turn on PT here. Just tell crashlogger to dump the
            // trace buffers if they're available.
            argv_crashlogger[argc_crashlogger++] = "-pton";
        }
        devmgr_launch(svcs_job_handle, "crashlogger",
                      argc_crashlogger, argv_crashlogger,
                      NULL, -1, NULL, NULL, 0);
    }

    mx_channel_create(0, &svc_root_handle, &svc_request_handle);

    start_console_shell();

    if (secondary_bootfs_ready()) {
        devmgr_start_appmgr(NULL);
    }

    thrd_t t;
    if ((thrd_create_with_name(&t, service_starter, NULL, "service-starter")) == thrd_success) {
        thrd_detach(t);
    }

    devmgr_handle_messages();
    printf("devmgr: message handler returned?!\n");
    return 0;
}
