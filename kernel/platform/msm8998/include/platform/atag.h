// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#define RPI_ATAGS_ADDRESS (0xffff000000000100)

#define RPI_ATAG_ATAG_NONE      0x00000000
#define RPI_ATAG_ATAG_CORE      0x54410001
#define RPI_ATAG_ATAG_MEM       0x54410002
#define RPI_ATAG_ATAG_VIDEOTEXT 0x54410003
#define RPI_ATAG_ATAG_RAMDISK   0x54410004
#define RPI_ATAG_ATAG_INITRD2   0x54420005
#define RPI_ATAG_ATAG_SERIAL    0x54410006
#define RPI_ATAG_ATAG_REVISION  0x54410007
#define RPI_ATAG_ATAG_VIDEOLFB  0x54410008
#define RPI_ATAG_ATAG_CMDLINE   0x54410009


struct atag_core {
    uint32_t    flags;
    uint32_t    page_size;
    uint32_t    root_dev;
};

struct atag_mem {
    uint32_t    size;
    uint32_t    start;
};

struct atag_videotext {
    uint8_t     x;
    uint8_t     y;
    uint16_t    video_page;
    uint8_t     video_mode;
    uint8_t     video_cols;
    uint16_t    video_ega_bx;
    uint8_t     video_lines;
    uint8_t     video_isvga;
    uint16_t    video_points;
};

struct atag_ramdisk {
    uint32_t    flags;
    uint32_t    size;
    uint32_t    start;
};

struct atag_initrd2 {
    uint32_t    start;
    uint32_t    size;
};

struct atag_serial {
    uint32_t    lsw;
    uint32_t    msw;
};

struct atag_revision {
    uint32_t    revision;
};

struct atag_videobuffer {
    uint16_t    lfb_width;
    uint16_t    lfb_height;
    uint16_t    lfb_depth;
    uint16_t    lfb_linelength;
    uint32_t    lfb_base;
    uint32_t    lfb_size;
    uint8_t     red_size;
    uint8_t     red_pos;
    uint8_t     green_size;
    uint8_t     green_pos;
    uint8_t     blue_size;
    uint8_t     blue_pos;
    uint8_t     rsvd_size;
    uint8_t     svd_pos;
};

typedef struct atag_core        atag_core_t;
typedef struct atag_mem         atag_mem_t;
typedef struct atag_videotext   atag_videotext_t;
typedef struct atag_ramdisk     atag_ramdisk_t;
typedef struct atag_initrd2     atag_initrd2_t;
typedef struct atag_serial      atag_serial_t;
typedef struct atag_revision    atag_revision_t;
typedef struct atag_videobuffer atag_videobuffer_t;

typedef struct atag {
    uint32_t    len;
    uint32_t    id;
    union {
        atag_core_t         core;
        atag_mem_t          mem;
        atag_videotext_t    videotext;
        atag_ramdisk_t      ramdisk;
        atag_initrd2_t      initrd2;
        atag_serial_t       serial;
        atag_revision_t     revision;
        atag_videobuffer_t  videobuffer;
        uint8_t             cmdline[1];
    };
}   atag_t;

atag_t * atag_find(uint32_t tag_id, uintptr_t ptr);
