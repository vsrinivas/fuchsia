// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/boot/bootdata.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

mx_status_t bootfs_parse(mx_handle_t vmo, size_t len,
                         mx_status_t (*cb)(void* cookie, const bootfs_entry_t* entry),
                         void* cookie) {
    bootfs_header_t hdr;
    size_t rlen;

    mx_status_t r = mx_vmo_read(vmo, &hdr, 0, sizeof(hdr), &rlen);
    if ((r < 0) || (rlen < sizeof(hdr))) {
        printf("bootfs_parse: couldn't read boot_data - %#zx\n", rlen);
        return MX_ERR_IO;
    }

    if (hdr.magic != BOOTFS_MAGIC) {
        printf("bootfs_parse: incorrect bootdata header: %08x\n", hdr.magic);
        return MX_ERR_IO;
    }

    //TODO: mmap instead
    if (hdr.dirsize > 65536) {
        printf("bootfs_parse: directory too large\n");
        return MX_ERR_OUT_OF_RANGE;
    }

    char buffer[hdr.dirsize];
    r = mx_vmo_read(vmo, buffer, sizeof(bootfs_header_t), hdr.dirsize, &rlen);
    if ((r < 0) || (rlen < hdr.dirsize)) {
        printf("bootfs_parse: could not read directory\n");
    }

    size_t avail = hdr.dirsize;
    void* p = buffer;
    while (avail > sizeof(bootfs_entry_t)) {
        bootfs_entry_t* e = p;

        size_t sz = BOOTFS_RECSIZE(e);
        if ((e->name_len < 1) || (e->name_len > BOOTFS_MAX_NAME_LEN) || (sz > avail)) {
            return MX_ERR_IO;
        }

        e->name[e->name_len - 1] = 0;
        if ((r = cb(cookie, e)) != MX_OK) {
            return r;
        }

        p += sz;
        avail -= sz;
    }
    return MX_OK;
}
