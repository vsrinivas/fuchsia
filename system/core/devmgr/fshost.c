// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devcoordinator.h"
#include "devmgr.h"
#include "memfs-private.h"

#include <bootdata/decompress.h>

#include <fdio/util.h>

#include <fs/vfs.h>

#include <zircon/boot/bootdata.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <stdbool.h>
#include <stdio.h>

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

static void start_system_init(void) {
    thrd_t t;
    int r = thrd_create_with_name(&t, devmgr_start_appmgr, NULL, "system-init");
    if (r == thrd_success) {
        thrd_detach(t);
    }
}

static bool has_secondary_bootfs = false;

bool secondary_bootfs_ready(void) {
    return has_secondary_bootfs;
}

static ssize_t setup_bootfs_vmo(uint32_t n, uint32_t type, zx_handle_t vmo) {
    uint64_t size;
    zx_status_t status = zx_vmo_get_size(vmo, &size);
    if (status != ZX_OK) {
        printf("devmgr: failed to get bootfs#%u size (%d)\n", n, status);
        return status;
    }
    if (size == 0) {
        return 0;
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
        memfs_mount(vfs_create_global_root(), systemfs_get_root());
    }
    bootfs_t bfs;
    if (bootfs_create(&bfs, vmo) == ZX_OK) {
        bootfs_parse(&bfs, callback, &cd);
        bootfs_destroy(&bfs);
    }
    return cd.file_count;
}

static void setup_last_crashlog(zx_handle_t vmo_in, uint64_t off_in, size_t sz) {
    printf("devmgr: last crashlog is %zu bytes\n", sz);
    zx_handle_t vmo;
    if (copy_vmo(vmo_in, off_in, sz, &vmo) != ZX_OK) {
        return;
    }
    bootfs_add_file("log/last-panic.txt", vmo, 0, sz);
}

static zx_status_t devmgr_read_mdi(zx_handle_t vmo, zx_off_t offset, size_t length) {
    zx_handle_t mdi_handle;
    zx_status_t status = copy_vmo(vmo, offset, length, &mdi_handle);
    if (status != ZX_OK) {
        printf("devmgr_read_mdi failed to copy MDI data: %d\n", status);
        return status;
    }

    devmgr_set_mdi(mdi_handle);
    return ZX_OK;

fail:
    printf("devmgr_read_mdi failed %d\n", status);
    zx_handle_close(mdi_handle);
    return status;
}

#define HND_BOOTFS(n) PA_HND(PA_VMO_BOOTFS, n)
#define HND_BOOTDATA(n) PA_HND(PA_VMO_BOOTDATA, n)

static void setup_bootfs(void) {
    zx_handle_t vmo;
    unsigned idx = 0;

    if ((vmo = zx_get_startup_handle(HND_BOOTFS(0)))) {
        setup_bootfs_vmo(idx++, BOOTDATA_BOOTFS_BOOT, vmo);
    } else {
        printf("devmgr: missing primary bootfs?!\n");
    }

    for (unsigned n = 0; (vmo = zx_get_startup_handle(HND_BOOTDATA(n))); n++) {
        bootdata_t bootdata;
        size_t actual;
        zx_status_t status = zx_vmo_read(vmo, &bootdata, 0, sizeof(bootdata), &actual);
        if ((status < 0) || (actual != sizeof(bootdata))) {
            goto done;
        }
        if ((bootdata.type != BOOTDATA_CONTAINER) || (bootdata.extra != BOOTDATA_MAGIC)) {
            printf("devmgr: bootdata item does not contain bootdata\n");
            goto done;
        }

        size_t len = bootdata.length;
        size_t off = sizeof(bootdata);
        if (bootdata.flags & BOOTDATA_FLAG_EXTRA) {
            off += sizeof(bootextra_t);
        }

        while (len > sizeof(bootdata)) {
            zx_status_t status = zx_vmo_read(vmo, &bootdata, off, sizeof(bootdata), &actual);
            if ((status < 0) || (actual != sizeof(bootdata))) {
                break;
            }
            size_t hdrsz = sizeof(bootdata_t);
            if (bootdata.flags & BOOTDATA_FLAG_EXTRA) {
                hdrsz += sizeof(bootextra_t);
            }
            size_t itemlen = BOOTDATA_ALIGN(hdrsz + bootdata.length);
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
                                             off, bootdata.length + hdrsz,
                                             &bootfs_vmo, &errmsg);
                if (status < 0) {
                    printf("devmgr: failed to decompress bootdata: %s\n", errmsg);
                } else {
                    setup_bootfs_vmo(idx++, bootdata.type, bootfs_vmo);
                }
                break;
            }
            case BOOTDATA_LAST_CRASHLOG:
                setup_last_crashlog(vmo, off + hdrsz, bootdata.length);
                break;
            case BOOTDATA_MDI:
                devmgr_read_mdi(vmo, off, itemlen);
                break;
            case BOOTDATA_CMDLINE:
            case BOOTDATA_ACPI_RSDP:
            case BOOTDATA_FRAMEBUFFER:
            case BOOTDATA_E820_TABLE:
            case BOOTDATA_EFI_MEMORY_MAP:
            case BOOTDATA_EFI_SYSTEM_TABLE:
            case BOOTDATA_DEBUG_UART:
            case BOOTDATA_LASTLOG_NVRAM:
            case BOOTDATA_LASTLOG_NVRAM2:
            case BOOTDATA_IGNORE:
                // quietly ignore these
                break;
            default:
                printf("devmgr: ignoring bootdata type=%08x size=%u\n",
                       bootdata.type, bootdata.length);
            }
            off += itemlen;
            len -= itemlen;
        }
done:
        zx_handle_close(vmo);
    }
}

ssize_t devmgr_add_systemfs_vmo(zx_handle_t vmo) {
    ssize_t added = setup_bootfs_vmo(100, BOOTDATA_BOOTFS_SYSTEM, vmo);
    if (added > 0) {
        start_system_init();
    }
    return added;
}

void fshost_start(void) {
    setup_bootfs();

    vfs_global_init(vfs_create_global_root());
}

zx_handle_t fs_root_clone(void) {
    return vfs_create_global_root_handle();
}