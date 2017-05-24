// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootfs.h"
#include "util.h"

#pragma GCC visibility push(hidden)

#include <magenta/boot/bootdata.h>
#include <magenta/syscalls.h>
#include <string.h>

#pragma GCC visibility pop

void bootfs_mount(mx_handle_t vmar, mx_handle_t log, mx_handle_t vmo, struct bootfs *fs) {
    uint64_t size;
    mx_status_t status = mx_vmo_get_size(vmo, &size);
    check(log, status, "mx_vmo_get_size failed on bootfs vmo\n");
    uintptr_t addr = 0;
    status = mx_vmar_map(vmar, 0, vmo, 0, size, MX_VM_FLAG_PERM_READ, &addr);
    check(log, status, "mx_vmar_map failed on bootfs vmo\n");
    fs->contents = (const void*)addr;
    fs->len = size;
    status = mx_handle_duplicate(
        vmo,
        MX_RIGHT_READ | MX_RIGHT_EXECUTE | MX_RIGHT_MAP |
        MX_RIGHT_TRANSFER | MX_RIGHT_DUPLICATE | MX_RIGHT_GET_PROPERTY,
        &fs->vmo);
    check(log, status, "mx_handle_duplicate failed on bootfs VMO handle\n");
}

void bootfs_unmount(mx_handle_t vmar, mx_handle_t log, struct bootfs *fs) {
    mx_status_t status = mx_vmar_unmap(vmar, (uintptr_t)fs->contents, fs->len);
    check(log, status, "mx_vmar_unmap failed\n");
    status = mx_handle_close(fs->vmo);
    check(log, status, "mx_handle_close failed\n");
}

struct bootfs_magic {
    bootdata_t boothdr;
    char fsmagic[16];
};

struct bootfs_file {
    uint32_t size, offset;
};

struct bootfs_header {
    uint32_t namelen;
    struct bootfs_file file;
};

static struct bootfs_file bootfs_search(mx_handle_t log,
                                        struct bootfs *fs,
                                        const char* filename) {
    static const char FSMAGIC[16] = "[BOOTFS]\0\0\0\0\0\0\0\0";
    size_t magic_size = sizeof(bootdata_t);
    if (fs->len < sizeof(struct bootfs_magic))
        fail(log, ERR_INVALID_ARGS, "bootfs image too small!\n");
    struct bootfs_magic* magic = (struct bootfs_magic*)fs->contents;
    if (magic->boothdr.type != BOOTDATA_BOOTFS_BOOT)
        fail(log, ERR_INVALID_ARGS, "bootdata is not a bootfs!\n");
    // This field is obsolete, so we can skip it if it doesn't exist.
    if (!memcmp(magic->fsmagic, FSMAGIC, sizeof(FSMAGIC))) {
        magic_size = sizeof(struct bootfs_magic);
    }
    const uint8_t* p = &fs->contents[magic_size];

    size_t filename_len = strlen(filename) + 1;

    while ((size_t)(p - fs->contents) < fs->len) {
        struct bootfs_header header;
        memcpy(&header, p, sizeof(header));
        p += sizeof(header);
        size_t left = fs->len - (p - fs->contents);

        if (header.namelen == 0)
            break;

        if (header.namelen > left)
            fail(log, ERR_INVALID_ARGS,
                 "bootfs has bogus namelen in header\n");

        const char* name = (const void*)p;
        p += header.namelen;

        if (!memcmp(name, filename, filename_len))
            return header.file;
    }

    struct bootfs_file runt = { 0, 0 };
    return runt;
}

mx_handle_t bootfs_open(mx_handle_t log, const char* purpose,
                        struct bootfs *fs, const char* filename) {
    print(log, "searching bootfs for ", purpose,
          " \"", filename, "\"\n", NULL);

    struct bootfs_file file = bootfs_search(log, fs, filename);
    if (file.offset == 0 && file.size == 0)
        fail(log, ERR_INVALID_ARGS, "file not found\n");
    if (file.offset > fs->len)
        fail(log, ERR_INVALID_ARGS, "bogus offset in bootfs header!\n");
    if (fs->len - file.offset < file.size)
        fail(log, ERR_INVALID_ARGS, "bogus size in bootfs header!\n");

    // Clone a private copy of the file's subset of the bootfs VMO.
    // TODO(mcgrathr): Create a plain read-only clone when the feature
    // is implemented in the VM.
    mx_handle_t vmo;
    mx_status_t status = mx_vmo_clone(fs->vmo, MX_VMO_CLONE_COPY_ON_WRITE,
                                      file.offset, file.size, &vmo);
    if (status != NO_ERROR)
        fail(log, status, "mx_vmo_clone failed\n");
    status = mx_object_set_property(vmo, MX_PROP_NAME,
                                    filename, strlen(filename));
    if (status != NO_ERROR)
        fail(log, status, "mx_object_set_property failed for VMO name\n");
    // Drop unnecessary MX_RIGHT_WRITE rights.
    // TODO(mcgrathr): Should be superfluous with read-only mx_vmo_clone.
    status = mx_handle_replace(
        vmo,
        MX_RIGHT_READ | MX_RIGHT_EXECUTE | MX_RIGHT_MAP |
        MX_RIGHT_TRANSFER | MX_RIGHT_DUPLICATE | MX_RIGHT_GET_PROPERTY,
        &vmo);
    if (status != NO_ERROR)
        fail(log, status, "mx_handle_replace failed\n");

    return vmo;
}
