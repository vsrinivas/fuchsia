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

#define BOOTFS_MAX_NAME_LEN 256


// BOOTFS is a trivial "filesystem" format
//
// It has a bootdata item header
// Followed by a series of records of:
//   namelength (32bit le)
//   filesize   (32bit le)
//   fileoffset (32bit le)
//   namedata   (namelength bytes, includes \0)
//
// - fileoffsets must be page aligned (multiple of 4096)

#define NLEN 0
#define FSIZ 1
#define FOFF 2

void bootfs_parse(mx_handle_t vmo, size_t len,
                  void (*cb)(void*, const char* fn, size_t off, size_t len),
                  void* cb_arg) {
    size_t rlen;
    bootdata_t hdr;
    size_t off = 0;
    mx_status_t r = mx_vmo_read(vmo, &hdr, off, sizeof(hdr), &rlen);
    if (r < 0 || rlen < sizeof(hdr)) {
        printf("bootfs_parse: couldn't read boot_data - %#zx\n", rlen);
        return;
    }

    if ((hdr.type & BOOTDATA_BOOTFS_MASK) != BOOTDATA_BOOTFS_TYPE) {
        printf("bootfs_parse: incorrect bootdata header: %08x\n", hdr.type);
        return;
    }

    uint8_t _buffer[4096];
    uint8_t* data = _buffer;
    uint8_t* end = data; // force initial read

    char name[BOOTFS_MAX_NAME_LEN];
    uint32_t header[3];

    off += sizeof(hdr);
    if (hdr.flags & BOOTDATA_FLAG_EXTRA) {
        off += sizeof(bootextra_t);
    }

    for (;;) {
        if ((end - data) < (int)sizeof(header)) {
            // read in another xxx headers
            off += data - _buffer; // advance past processed headers
            r = mx_vmo_read(vmo, _buffer, off, sizeof(_buffer), &rlen);
            if (r < 0) {
                break;
            }
            data = _buffer;
            end = data+rlen;
            if ((end - data) < (int)sizeof(header)) {
                break;
            }
        }
        memcpy(header, data, sizeof(header));

        // check for end marker
        if (header[NLEN] == 0)
            break;

        // require reasonable filename size
        if ((header[NLEN] < 2) || (header[NLEN] > BOOTFS_MAX_NAME_LEN)) {
            printf("bootfs_parse: bogus filename\n");
            break;
        }

        // require correct alignment
        if (header[FOFF] & 4095) {
            printf("bootfs_parse: unaligned item\n");
            break;
        }


        if (data + sizeof(header) + header[NLEN] > end) {
            // read only part of the last file name:
            // back up end and induce a fresh, new read
            end = data;
            continue;
        }
        data += sizeof(header);
        if ((end - data) < (off_t)header[NLEN]) {
            break;
        }
        memcpy(name, data, header[NLEN]);
        data += header[NLEN];
        name[header[NLEN] - 1] = 0;

        (*cb)(cb_arg, name, header[FOFF], header[FSIZ]);
    }
}
