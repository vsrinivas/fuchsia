// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <stdint.h>

// lsw of sha256("bootdata")
#define BOOTDATA_MAGIC (0x868cf7e6u)

// Align bootdata_t to 8 byte boundary
#define BOOTDATA_ALIGN(n) (((n) + 7) & (~7))

__BEGIN_CDECLS;

// Containers are used to wrap a set of bootdata items
// written to a file or partition.  The "length" is the
// length of the set of following bootdata items.  The
// "extra" is the value BOOTDATA_MAGIC and "flags" is
// set to 0.
#define BOOTDATA_CONTAINER        (0x544f4f42u) // BOOT

// BOOTFS images.  The "extra" field is the decompressed
// size of the image, if compressed, otherwise the same
// as the "length" field.
#define BOOTDATA_BOOTFS_BOOT      (0x42534642u) // BFSB
#define BOOTDATA_BOOTFS_SYSTEM    (0x53534642u) // BFSS
#define BOOTDATA_BOOTFS_DISCARD   (0x58534642u) // BFSX

#define BOOTDATA_BOOTFS_MASK      (0x00FFFFFFu)
#define BOOTDATA_BOOTFS_TYPE      (0x00534642u) // BFS\0

// MDI data.  The "extra" field is unused and set to 0.
#define BOOTDATA_MDI              (0x3149444du) // MDI1

// Flag indicating that the bootfs is compressed.
#define BOOTDATA_BOOTFS_FLAG_COMPRESSED  (1 << 0)


// BootData header, describing the type and size of data
// used to initialize the system. All fields are little-endian.
//
// BootData headers in a stream must be 8-byte-aligned.
//
// The length field specifies the actual payload length
// and does not include the size of padding.  The macro
// BOOTDATA_ALIGN(length) may be used to determine the
// number of padding bytes to insert or skip past.
typedef struct {
    // Boot data type
    uint32_t type;

    // Size of the payload following this header
    uint32_t length;

    // type-specific extra data
    // For CONTAINER this is MAGIC.
    // For BOOTFS this is the decompressed size.
    uint32_t extra;

    // Flags for the boot data. See flag descriptions for each type.
    uint32_t flags;
} bootdata_t;

__END_CDECLS;
