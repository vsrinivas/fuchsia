// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devmgr.h"
#include "memfs-private.h"

#include <bootdata/decompress.h>

#include <fs-management/ramdisk.h>
#include <launchpad/launchpad.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/util.h>
#include <lib/fdio/watcher.h>
#include <loader-service/loader-service.h>

#include <zircon/boot/bootdata.h>
#include <zircon/device/vfs.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include "block-watcher.h"
#include "bootfs.h"

// When adding VMOs to the boot filesystem, add them under the directory
// /boot/VMO_SUBDIR. This constant must end, but not start, with a slash.
#define VMO_SUBDIR "kernel/"
#define VMO_SUBDIR_LEN (sizeof(VMO_SUBDIR) - 1)

#define LAST_PANIC_FILEPATH "log/last-panic.txt"

struct callback_data {
    zx_handle_t vmo;
    unsigned int file_count;
    zx_status_t (*add_file)(const char* path, zx_handle_t vmo, zx_off_t off, size_t len);
};

static zx_status_t callback(void* arg, const bootfs_entry_t* entry) {
    struct callback_data* cd = arg;
    //printf("bootfs: %s @%zd (%zd bytes)\n", path, off, len);
    cd->add_file(entry->name, cd->vmo, entry->data_off, entry->data_len);
    ++cd->file_count;
    return ZX_OK;
}

static bool has_secondary_bootfs = false;

bool secondary_bootfs_ready(void) {
    return has_secondary_bootfs;
}

static zx_status_t setup_bootfs_vmo(uint32_t n, uint32_t type, zx_handle_t vmo) {
    uint64_t size;
    zx_status_t status = zx_vmo_get_size(vmo, &size);
    if (status != ZX_OK) {
        printf("devmgr: failed to get bootfs#%u size (%d)\n", n, status);
        return status;
    }
    if (size == 0) {
        return ZX_OK;
    }

    // map the vmo so that ps will account for it
    // NOTE: will leak the mapping in case the bootfs is thrown away later
    uintptr_t address;
    zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ, &address);

    struct callback_data cd = {
        .vmo = vmo,
        .add_file = (type == BOOTDATA_BOOTFS_SYSTEM) ? systemfs_add_file : bootfs_add_file,
    };
    if ((type == BOOTDATA_BOOTFS_SYSTEM) && !has_secondary_bootfs) {
        has_secondary_bootfs = true;
        status = memfs_mount(vfs_create_global_root(), "system", systemfs_get_root());
        if (status != ZX_OK) {
            printf("devmgr: failed to mount /system (%d)\n", status);
            return status;
        }
    }
    // We need to duplicate |vmo| because |bootfs_create| takes ownership of the
    // |vmo| and closes it during |bootfs_destroy|. However, we've stored |vmo|
    // in |cd|, and |callback| will further store |vmo| in memfs.
    zx_handle_t bootfs_vmo;
    status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &bootfs_vmo);
    if (status != ZX_OK) {
        printf("devmgr: failed to duplicate vmo for /system (%d)\n", status);
        return status;
    }
    bootfs_t bfs;
    if (bootfs_create(&bfs, bootfs_vmo) == ZX_OK) {
        bootfs_parse(&bfs, callback, &cd);
        bootfs_destroy(&bfs);
    } else {
        zx_handle_close(bootfs_vmo);
    }
    if (type == BOOTDATA_BOOTFS_SYSTEM) {
        // TODO(abarth): Uncomment this line when we're ready to make /system
        // read only.
        // systemfs_set_readonly(getenv("zircon.system.writable") == NULL);
    }
    return ZX_OK;
}

static void setup_last_crashlog(zx_handle_t vmo_in, uint64_t off_in, size_t sz) {
    printf("devmgr: last crashlog is %zu bytes\n", sz);
    zx_handle_t vmo;
    if (copy_vmo(vmo_in, off_in, sz, &vmo) != ZX_OK) {
        return;
    }
    bootfs_add_file(LAST_PANIC_FILEPATH, vmo, 0, sz);
}

struct bootdata_ramdisk {
    struct bootdata_ramdisk* next;
    zx_handle_t vmo;
};
static struct bootdata_ramdisk* bootdata_ramdisk_list;

static zx_status_t misc_device_added(int dirfd, int event, const char* fn,
                                     void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE || strcmp(fn, "ramctl") != 0) {
        return ZX_OK;
    }

    while (bootdata_ramdisk_list != NULL) {
        struct bootdata_ramdisk* br = bootdata_ramdisk_list;
        bootdata_ramdisk_list = br->next;
        zx_handle_t ramdisk_vmo = br->vmo;
        free(br);

        uint64_t size;
        zx_vmo_get_size(ramdisk_vmo, &size);

        char path[PATH_MAX + 1];
        if (create_ramdisk_from_vmo(ramdisk_vmo, path) < 0) {
            printf("fshost: failed to create ramdisk from BOOTDATA_RAMDISK\n");
        } else {
            printf("fshost: BOOTDATA_RAMDISK attached as %s\n", path);
        }
    }

    return ZX_ERR_STOP;
}

static int ramctl_watcher(void* arg) {
    int dirfd = open("/dev/misc", O_DIRECTORY | O_RDONLY);
    if (dirfd < 0) {
        printf("fshost: failed to open /dev/misc: %s\n", strerror(errno));
        return -1;
    }
    fdio_watch_directory(dirfd, &misc_device_added, ZX_TIME_INFINITE, NULL);
    close(dirfd);
    return 0;
}

#define HND_BOOTFS(n) PA_HND(PA_VMO_BOOTFS, n)
#define HND_BOOTDATA(n) PA_HND(PA_VMO_BOOTDATA, n)

static void setup_bootfs(void) {
    zx_handle_t vmo;
    unsigned idx = 0;

    if ((vmo = zx_take_startup_handle(HND_BOOTFS(0)))) {
        setup_bootfs_vmo(idx++, BOOTDATA_BOOTFS_BOOT, vmo);
    } else {
        printf("devmgr: missing primary bootfs?!\n");
    }

    for (unsigned n = 0; (vmo = zx_take_startup_handle(HND_BOOTDATA(n))); n++) {
        bootdata_t bootdata;
        zx_status_t status = zx_vmo_read(vmo, &bootdata, 0, sizeof(bootdata));
        if (status < 0) {
            goto done;
        }
        if ((bootdata.type != BOOTDATA_CONTAINER) || (bootdata.extra != BOOTDATA_MAGIC)) {
            printf("devmgr: bootdata item does not contain bootdata\n");
            goto done;
        }
        if (!(bootdata.flags & BOOTDATA_FLAG_V2)) {
            printf("devmgr: bootdata v1 no longer supported\n");
            goto done;
        }

        size_t len = bootdata.length;
        size_t off = sizeof(bootdata);

        while (len > sizeof(bootdata)) {
            zx_status_t status = zx_vmo_read(vmo, &bootdata, off, sizeof(bootdata));
            if (status < 0) {
                break;
            }
            size_t itemlen = BOOTDATA_ALIGN(sizeof(bootdata_t) + bootdata.length);
            if (itemlen > len) {
                printf("devmgr: bootdata item too large (%zd > %zd)\n", itemlen, len);
                break;
            }
            switch (bootdata.type) {
            case BOOTDATA_CONTAINER:
                printf("devmgr: unexpected bootdata container header\n");
                goto done;
            case BOOTDATA_BOOTFS_DISCARD:
                // this was already unpacked for us by userboot
                break;
            case BOOTDATA_BOOTFS_BOOT:
            case BOOTDATA_BOOTFS_SYSTEM: {
                const char* errmsg;
                zx_handle_t bootfs_vmo;
                status = decompress_bootdata(zx_vmar_root_self(), vmo,
                                             off, bootdata.length + sizeof(bootdata_t),
                                             &bootfs_vmo, &errmsg);
                if (status < 0) {
                    printf("devmgr: failed to decompress bootdata: %s\n", errmsg);
                } else {
                    setup_bootfs_vmo(idx++, bootdata.type, bootfs_vmo);
                }
                break;
            }
            case BOOTDATA_RAMDISK: {
                const char* errmsg;
                zx_handle_t ramdisk_vmo;
                status = decompress_bootdata(
                    zx_vmar_root_self(), vmo,
                    off, bootdata.length + sizeof(bootdata_t),
                    &ramdisk_vmo, &errmsg);
                if (status != ZX_OK) {
                    printf("fshost: failed to decompress bootdata: %s\n",
                           errmsg);
                } else {
                    struct bootdata_ramdisk* br = malloc(sizeof(*br));
                    br->vmo = ramdisk_vmo;
                    br->next = bootdata_ramdisk_list;
                    bootdata_ramdisk_list = br;
                }
                break;
            }
            case BOOTDATA_LAST_CRASHLOG:
                setup_last_crashlog(vmo, off + sizeof(bootdata_t), bootdata.length);
                break;
            default:
                break;
            }
            off += itemlen;
            len -= itemlen;
        }
    done:
        zx_handle_close(vmo);
    }
}

// Look for VMOs passed as startup handles of PA_HND_TYPE type, and add them to
// the filesystem under the path /boot/VMO_SUBDIR_LEN/<vmo-name>.
static void fetch_vmos(uint_fast8_t type, const char* debug_type_name) {
    for (uint_fast16_t i = 0; true; ++i) {
        zx_handle_t vmo = zx_take_startup_handle(PA_HND(type, i));
        if (vmo == ZX_HANDLE_INVALID)
            break;

        if (type == PA_VMO_VDSO && i == 0) {
            // The first vDSO is the default vDSO.  Since we've stolen
            // the startup handle, launchpad won't find it on its own.
            // So point launchpad at it.
            launchpad_set_vdso_vmo(vmo);
        }

        // The vDSO VMOs have names like "vdso/default", so those
        // become VMO files at "/boot/kernel/vdso/default".
        char name[VMO_SUBDIR_LEN + ZX_MAX_NAME_LEN] = VMO_SUBDIR;
        size_t size;
        zx_status_t status = zx_object_get_property(vmo, ZX_PROP_NAME,
                                                    name + VMO_SUBDIR_LEN, sizeof(name) - VMO_SUBDIR_LEN);
        if (status != ZX_OK) {
            printf("devmgr: zx_object_get_property on %s %u: %s\n",
                   debug_type_name, i, zx_status_get_string(status));
            continue;
        }
        status = zx_vmo_get_size(vmo, &size);
        if (status != ZX_OK) {
            printf("devmgr: zx_vmo_get_size on %s %u: %s\n",
                   debug_type_name, i, zx_status_get_string(status));
            continue;
        }
        if (size == 0) {
            // empty vmos do not get installed
            zx_handle_close(vmo);
            continue;
        }
        if (!strcmp(name + VMO_SUBDIR_LEN, "crashlog")) {
            // the crashlog has a special home
            strcpy(name, LAST_PANIC_FILEPATH);
        }
        status = bootfs_add_file(name, vmo, 0, size);
        if (status != ZX_OK) {
            printf("devmgr: failed to add %s %u to filesystem: %s\n",
                   debug_type_name, i, zx_status_get_string(status));
        }
    }
}

static zx_handle_t devfs_root;
static zx_handle_t svc_root;
static zx_handle_t fshost_event;

void fshost_start(void) {
    setup_bootfs();

    vfs_global_init(vfs_create_global_root());
    vfs_watch_exit(fshost_event);

    fetch_vmos(PA_VMO_VDSO, "PA_VMO_VDSO");
    fetch_vmos(PA_VMO_KERNEL_FILE, "PA_VMO_KERNEL_FILE");

    // if we have a /system ramdisk, start higher level services
    if (has_secondary_bootfs) {
        fuchsia_start();
    }
}

zx_handle_t fs_root_clone(void) {
    zx_handle_t h;
    zx_status_t status = vfs_create_global_root_handle(&h);
    if (status != ZX_OK)
        return ZX_HANDLE_INVALID;
    return h;
}

static zx_handle_t fs_root;
static zx_handle_t devfs_root;
static zx_handle_t svc_root;
static zx_handle_t fshost_event;

#define FS_DIR_FLAGS                            \
    (ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_ADMIN | \
     ZX_FS_FLAG_DIRECTORY | ZX_FS_FLAG_NOREMOTE)

zx_handle_t fs_clone(const char* path) {
    if (!strcmp(path, "svc")) {
        return fdio_service_clone(svc_root);
    }
    if (!strcmp(path, "dev")) {
        return fdio_service_clone(devfs_root);
    }
    zx_handle_t h0, h1;
    if (zx_channel_create(0, &h0, &h1) != ZX_OK) {
        return ZX_HANDLE_INVALID;
    }
    if (fdio_open_at(fs_root, path, FS_DIR_FLAGS, h1) != ZX_OK) {
        zx_handle_close(h0);
        return ZX_HANDLE_INVALID;
    }
    return h0;
}

void fuchsia_start(void) {
    zx_object_signal(fshost_event, 0, FSHOST_SIGNAL_READY);
}

static loader_service_t* loader_service;

int main(int argc, char** argv) {
    printf("fshost: started.\n");

    bool netboot = false;
    while (argc > 1) {
        if (!strcmp(argv[1], "--netboot")) {
            netboot = true;
        } else {
            printf("fshost: unknown option '%s'\n", argv[1]);
        }
        argc--;
        argv++;
    }

    zx_handle_t _fs_root = zx_take_startup_handle(PA_HND(PA_USER0, 0));
    devfs_root = zx_take_startup_handle(PA_HND(PA_USER0, 1));
    svc_root = zx_take_startup_handle(PA_HND(PA_USER0, 2));
    zx_handle_t devmgr_loader = zx_take_startup_handle(PA_HND(PA_USER0, 3));
    fshost_event = zx_take_startup_handle(PA_HND(PA_USER1, 0));

    fshost_start();

    vfs_connect_global_root_handle(_fs_root);

    fdio_ns_t* ns;
    zx_status_t r;
    if ((r = fdio_ns_create(&ns)) != ZX_OK) {
        printf("fshost: cannot create namespace: %d\n", r);
        return -1;
    }

    if ((r = fdio_ns_bind(ns, "/fs", (fs_root = fs_root_clone()))) != ZX_OK) {
        printf("fshost: cannot bind /fs to namespace: %d\n", r);
    }
    if ((r = fdio_ns_bind(ns, "/dev", fs_clone("dev"))) != ZX_OK) {
        printf("fshost: cannot bind /dev to namespace: %d\n", r);
    }
    if ((r = fdio_ns_bind(ns, "/boot", fs_clone("boot"))) != ZX_OK) {
        printf("devmgr: cannot bind /boot to namespace: %d\n", r);
    }
    if ((r = fdio_ns_bind(ns, "/system", fs_clone("system"))) != ZX_OK) {
        printf("devmgr: cannot bind /system to namespace: %d\n", r);
    }
    if ((r = fdio_ns_install(ns)) != ZX_OK) {
        printf("fshost: cannot install namespace: %d\n", r);
    }

    if ((r = loader_service_create_fs(NULL, &loader_service)) != ZX_OK) {
        printf("fshost: failed to create loader service: %d\n", r);
    } else {
        loader_service_attach(loader_service, devmgr_loader);
        zx_handle_t svc;
        if ((r = loader_service_connect(loader_service, &svc)) != ZX_OK) {
            printf("fshost: failed to connect to loader service: %d\n", r);
        } else {
            // switch from bootfs-loader to system-loader
            zx_handle_close(dl_set_loader_service(svc));
        }
    }

    if (bootdata_ramdisk_list != NULL) {
        thrd_t th;
        int err = thrd_create_with_name(&th, &ramctl_watcher, NULL,
                                        "ramctl-watcher");
        if (err != thrd_success) {
            printf("fshost: failed to start ramctl-watcher: %d\n", err);
            while (bootdata_ramdisk_list != NULL) {
                struct bootdata_ramdisk* br = bootdata_ramdisk_list;
                bootdata_ramdisk_list = br->next;
                zx_handle_close(br->vmo);
                free(br);
            }
        } else {
            thrd_detach(th);
        }
    }

    block_device_watcher(zx_job_default(), netboot);
}
