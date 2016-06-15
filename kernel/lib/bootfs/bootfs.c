// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <lib/bootfs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <err.h>
#include <lib/bio.h>

#define LOCAL_TRACE 0

static const char FSMAGIC[16] = "[BOOTFS]\0\0\0\0\0\0\0\0";

// BOOTFS is a trivial "filesystem" format
//
// It has a 16 byte magic/version value (FSMAGIC)
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

int bootfs_parse_bio(bdev_t *bdev, uint64_t offset, struct list_node *list) {
    char buf[16];
    ssize_t err;

    err = bio_read(bdev, buf, offset, sizeof(FSMAGIC));
    if (err < (ssize_t)sizeof(FSMAGIC)) {
        return ERR_INVALID_ARGS;
    }
    if (memcmp(buf, FSMAGIC, sizeof(FSMAGIC))) {
        LTRACEF("bootfs: bad magic\n");
        return ERR_INVALID_ARGS;
    }
    offset += sizeof(FSMAGIC);

    int found = 0;
    while (offset < (uint64_t)bdev->total_size) {
        uint32_t header[3];

        err = bio_read(bdev, &header, offset, sizeof(header));
        if (err < (ssize_t)sizeof(header)) {
            break;
        }
        offset += sizeof(header);

        // dump the header
        LTRACEF("bootfs: %d: namesize=%u filesize=%u offset=%u\n",
                found, header[NLEN], header[FSIZ], header[FOFF]);

        // check for end marker
        if (header[NLEN] == 0) break;

        // require reasonable filename size
        if ((header[NLEN] < 2) || (header[NLEN] > BOOTFS_MAX_NAME_LEN)) {
            dprintf(INFO, "bootfs: %d: bogus filename length\n", found);
            break;
        }

        // require correct alignment
        if (header[FOFF] & 4095) {
            dprintf(INFO, "bootfs: %d: badly aligned offset\n", found);
            break;
        }

        struct bootfs_file *file = malloc(sizeof(*file) + header[NLEN]);
        if (!file) break;

        err = bio_read(bdev, file->name, offset, header[NLEN]);
        if (err < (ssize_t)header[NLEN]) {
            free(file);
            break;
        }
        offset += header[NLEN];

        // last byte must be a \0, ensure that is true
        file->name[header[NLEN] - 1] = 0;

        // save the offset and length of the file
        file->offset = header[FOFF];
        file->len = header[FSIZ];
        file->bdev = bdev;

        // add this file to the list we're returning
        list_add_tail(list, &file->node);
        found++;
    }

    return found;
}



