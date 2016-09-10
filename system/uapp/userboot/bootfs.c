// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootfs.h"
#include "util.h"

#pragma GCC visibility push(hidden)

#include <magenta/syscalls.h>
#include <string.h>

#pragma GCC visibility pop

void bootfs_mount(mx_handle_t proc_self, mx_handle_t log, mx_handle_t vmo, struct bootfs *fs) {
    uint64_t size;
    mx_status_t status = mx_vmo_get_size(vmo, &size);
    check(log, status, "mx_vmo_get_size failed on bootfs vmo\n");
    uintptr_t addr = 0;
    status = mx_process_map_vm(proc_self, vmo, 0, size, &addr, MX_VM_FLAG_PERM_READ);
    check(log, status, "mx_process_map_vm failed on bootfs vmo\n");
    fs->contents =  (const void*)addr;
    fs->len = size;
}

void bootfs_unmount(mx_handle_t proc_self, mx_handle_t log, struct bootfs *fs) {
    mx_status_t status = mx_process_unmap_vm(proc_self, (uintptr_t)fs->contents, 0);
    check(log, status, "mx_process_unmap_vm failed\n");
}


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
    if (fs->len < sizeof(FSMAGIC))
        fail(log, ERR_INVALID_ARGS, "bootfs image too small!\n");
    if (memcmp(fs->contents, FSMAGIC, sizeof(FSMAGIC)))
        fail(log, ERR_INVALID_ARGS, "bootfs has bad magic number!\n");
    const uint8_t* p = &fs->contents[sizeof(FSMAGIC)];

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

mx_handle_t bootfs_open(mx_handle_t log,
                        struct bootfs *fs, const char* filename) {
    print(log, "searching bootfs for \"", filename, "\"\n", NULL);

    struct bootfs_file file = bootfs_search(log, fs, filename);
    if (file.offset == 0 && file.size == 0)
        fail(log, ERR_INVALID_ARGS, "file not found\n");
    if (file.offset > fs->len)
        fail(log, ERR_INVALID_ARGS, "bogus offset in bootfs header!\n");
    if (fs->len - file.offset < file.size)
        fail(log, ERR_INVALID_ARGS, "bogus size in bootfs header!\n");

    mx_handle_t vmo = mx_vmo_create(file.size);
    if (vmo < 0)
        fail(log, vmo, "mx_vmo_create failed\n");
    mx_ssize_t n = mx_vmo_write(vmo, &fs->contents[file.offset],
                                      0, file.size);
    if (n < 0)
        fail(log, n, "mx_vmo_write failed\n");
    if (n != (mx_ssize_t)file.size)
        fail(log, ERR_IO, "mx_vmo_write short write\n");

    return vmo;
}
