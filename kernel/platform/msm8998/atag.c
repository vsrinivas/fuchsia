// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <platform/atag.h>
#include <sys/types.h>
#include <stdbool.h>

static bool atag_isvalid(atag_t* tag) {
    switch (tag->id) {
    case RPI_ATAG_ATAG_NONE:
    case RPI_ATAG_ATAG_CORE:
    case RPI_ATAG_ATAG_MEM:
    case RPI_ATAG_ATAG_VIDEOTEXT:
    case RPI_ATAG_ATAG_RAMDISK:
    case RPI_ATAG_ATAG_INITRD2:
    case RPI_ATAG_ATAG_SERIAL:
    case RPI_ATAG_ATAG_REVISION:
    case RPI_ATAG_ATAG_VIDEOLFB:
    case RPI_ATAG_ATAG_CMDLINE:
        return 1;
    default:
        return 0;
    }
}

atag_t* atag_find(uint32_t tag_id, uintptr_t ptr) {

    atag_t* tag = (atag_t*)ptr;

    while (atag_isvalid(tag)) {
        if (tag->id == tag_id) {
            return tag;
        } else {
            if (tag->id == RPI_ATAG_ATAG_NONE) {
                return NULL;
            } else {
                tag = (atag_t*)((uint32_t*)tag + tag->len);
            }
        }
    }

    return NULL;
}
