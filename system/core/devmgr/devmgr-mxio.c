// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devcoordinator.h"
#include "devmgr.h"
#include "memfs-private.h"

#include <fs/vfs.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>

#include <bootdata/decompress.h>

#include <magenta/boot/bootdata.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>

#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>

#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void devmgr_io_init(void) {
    // setup stdout
    mx_handle_t h;
    if (mx_log_create(0, &h) < 0) {
        return;
    }
    mxio_t* logger;
    if ((logger = mxio_logger_create(h)) == NULL) {
        return;
    }
    close(1);
    mxio_bind_to_fd(logger, 1, 0);
}

struct callback_data {
    mx_handle_t vmo;
    unsigned int file_count;
    mx_status_t (*add_file)(const char* path, mx_handle_t vmo, mx_off_t off, size_t len);
};

static mx_status_t callback(void* arg, const bootfs_entry_t* entry) {
    struct callback_data* cd = arg;
    //printf("bootfs: %s @%zd (%zd bytes)\n", path, off, len);
    cd->add_file(entry->name, cd->vmo, entry->data_off, entry->data_len);
    ++cd->file_count;
    return MX_OK;
}

#define USER_MAX_HANDLES 4
#define MAX_ENVP 16
#define CHILD_JOB_RIGHTS (MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE)

mx_status_t devmgr_launch(mx_handle_t job, const char* name,
                          int argc, const char* const* argv,
                          const char** _envp, int stdiofd,
                          mx_handle_t* handles, uint32_t* types, size_t hcount,
                          mx_handle_t* proc) {

    const char* envp[MAX_ENVP + 1];
    unsigned envn = 0;

    if (getenv(LDSO_TRACE_CMDLINE)) {
        envp[envn++] = LDSO_TRACE_ENV;
    }
    while ((_envp && _envp[0]) && (envn < MAX_ENVP)) {
        envp[envn++] = *_envp++;
    }
    envp[envn++] = NULL;

    mx_handle_t job_copy = MX_HANDLE_INVALID;
    mx_handle_duplicate(job, CHILD_JOB_RIGHTS, &job_copy);

    launchpad_t* lp;
    launchpad_create(job_copy, name, &lp);
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, argv);
    launchpad_set_environ(lp, envp);

    const char* nametable[2] = { "/", "/svc", };
    size_t name_count = 0;

    mx_handle_t h = vfs_create_global_root_handle();
    launchpad_add_handle(lp, h, PA_HND(PA_NS_DIR, name_count++));

    //TODO: constrain to /svc/debug, or other as appropriate
    if (strcmp(name, "init") && ((h = get_service_root()) != MX_HANDLE_INVALID)) {
        launchpad_add_handle(lp, h, PA_HND(PA_NS_DIR, name_count++));
    }

    launchpad_set_nametable(lp, name_count, nametable);

    if (stdiofd < 0) {
        mx_status_t r;
        if ((r = mx_log_create(0, &h) < 0)) {
            launchpad_abort(lp, r, "devmgr: cannot create debuglog handle");
        } else {
            launchpad_add_handle(lp, h, PA_HND(PA_MXIO_LOGGER, MXIO_FLAG_USE_FOR_STDIO | 0));
        }
    } else {
        launchpad_clone_fd(lp, stdiofd, MXIO_FLAG_USE_FOR_STDIO | 0);
        close(stdiofd);
    }

    launchpad_add_handles(lp, hcount, handles, types);

    const char* errmsg;
    mx_status_t status = launchpad_go(lp, proc, &errmsg);
    if (status < 0) {
        printf("devmgr: launchpad %s (%s) failed: %s: %d\n",
               argv[0], name, errmsg, status);
    } else {
        printf("devmgr: launch %s (%s) OK\n", argv[0], name);
    }
    return status;
}

static void start_system_init(void) {
    thrd_t t;
    int r = thrd_create_with_name(&t, devmgr_start_appmgr, NULL, "system-init");
    if (r == thrd_success) {
        thrd_detach(t);
    }
}

static bool has_secondary_bootfs = false;
static ssize_t setup_bootfs_vmo(uint32_t n, uint32_t type, mx_handle_t vmo) {
    uint64_t size;
    mx_status_t status = mx_vmo_get_size(vmo, &size);
    if (status != MX_OK) {
        printf("devmgr: failed to get bootfs#%u size (%d)\n", n, status);
        return status;
    }
    if (size == 0) {
        return 0;
    }

    // map the vmo so that ps will account for it
    // NOTE: will leak the mapping in case the bootfs is thrown away later
    uintptr_t address;
    mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size, MX_VM_FLAG_PERM_READ, &address);

    struct callback_data cd = {
        .vmo = vmo,
        .add_file = (type == BOOTDATA_BOOTFS_SYSTEM) ? systemfs_add_file : bootfs_add_file,
    };
    if ((type == BOOTDATA_BOOTFS_SYSTEM) && !has_secondary_bootfs) {
        has_secondary_bootfs = true;
        memfs_mount(vfs_create_global_root(), systemfs_get_root());
    }
    bootfs_t bfs;
    if (bootfs_create(&bfs, vmo) == MX_OK) {
        bootfs_parse(&bfs, callback, &cd);
        bootfs_destroy(&bfs);
    }
    return cd.file_count;
}

static mx_status_t copy_vmo(mx_handle_t src, mx_off_t offset, size_t length, mx_handle_t* out_dest) {
    mx_handle_t dest;
    mx_status_t status = mx_vmo_create(length, 0, &dest);
    if (status != MX_OK) {
        return status;
    }

    char buffer[PAGE_SIZE];
    mx_off_t src_offset = offset;
    mx_off_t dest_offset = 0;

    while (length > 0) {
        size_t copy = (length > sizeof(buffer) ? sizeof(buffer) : length);
        size_t actual;
        if ((status = mx_vmo_read(src, buffer, src_offset, copy, &actual)) != MX_OK) {
            goto fail;
        }
        if ((status = mx_vmo_write(dest, buffer, dest_offset, actual, &actual)) != MX_OK) {
            goto fail;
        }
        src_offset += actual;
        dest_offset += actual;
        length -= actual;
    }

    *out_dest = dest;
    return MX_OK;

fail:
    mx_handle_close(dest);
    return status;
}

static void setup_last_crashlog(mx_handle_t vmo_in, uint64_t off_in, size_t sz) {
    printf("devmgr: last crashlog is %zu bytes\n", sz);
    mx_handle_t vmo;
    if (copy_vmo(vmo_in, off_in, sz, &vmo) != MX_OK) {
        return;
    }
    bootfs_add_file("log/last-panic.txt", vmo, 0, sz);
}

static mx_status_t devmgr_read_mdi(mx_handle_t vmo, mx_off_t offset, size_t length) {
    mx_handle_t mdi_handle;
    mx_status_t status = copy_vmo(vmo, offset, length, &mdi_handle);
    if (status != MX_OK) {
        printf("devmgr_read_mdi failed to copy MDI data: %d\n", status);
        return status;
    }

    devmgr_set_mdi(mdi_handle);
    return MX_OK;

fail:
    printf("devmgr_read_mdi failed %d\n", status);
    mx_handle_close(mdi_handle);
    return status;
}

#define HND_BOOTFS(n) PA_HND(PA_VMO_BOOTFS, n)
#define HND_BOOTDATA(n) PA_HND(PA_VMO_BOOTDATA, n)

static void setup_bootfs(void) {
    mx_handle_t vmo;
    unsigned idx = 0;

    if ((vmo = mx_get_startup_handle(HND_BOOTFS(0)))) {
        setup_bootfs_vmo(idx++, BOOTDATA_BOOTFS_BOOT, vmo);
    } else {
        printf("devmgr: missing primary bootfs?!\n");
    }

    for (unsigned n = 0; (vmo = mx_get_startup_handle(HND_BOOTDATA(n))); n++) {
        bootdata_t bootdata;
        size_t actual;
        mx_status_t status = mx_vmo_read(vmo, &bootdata, 0, sizeof(bootdata), &actual);
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
            mx_status_t status = mx_vmo_read(vmo, &bootdata, off, sizeof(bootdata), &actual);
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
                mx_handle_t bootfs_vmo;
                status = decompress_bootdata(mx_vmar_root_self(), vmo,
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
        mx_handle_close(vmo);
    }
}

ssize_t devmgr_add_systemfs_vmo(mx_handle_t vmo) {
    ssize_t added = setup_bootfs_vmo(100, BOOTDATA_BOOTFS_SYSTEM, vmo);
    if (added > 0) {
        start_system_init();
    }
    return added;
}

bool secondary_bootfs_ready(void) {
    return has_secondary_bootfs;
}

void devmgr_vfs_init(void) {
    printf("devmgr: vfs init\n");

    setup_bootfs();

    vfs_global_init(vfs_create_global_root());

    // give our own process access to files in the vfs
    mx_handle_t h = vfs_create_global_root_handle();
    if (h > 0) {
        mxio_install_root(mxio_remote_create(h, 0));
    }
}
