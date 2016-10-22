// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <stdint.h>

// "BOOTDATA" in ASCII (little-endian)
#define BOOTDATA_MAGIC 0x41544144544f4f42ULL

__BEGIN_CDECLS;

#define BOOTDATA_TYPE_BOOTFS 1

// Flag indicating that the bootfs is compressed.
#define BOOTDATA_BOOTFS_FLAG_COMPRESSED  (1 << 0)

// Boot data header, describing the type and size of data used to initialize the
// system. All fields are little-endian. Any changes to this struct must change
// the magic number as well.
typedef struct {
    // Magic number: must be set to value of BOOTDATA_MAGIC
    uint64_t magic;

    // Boot data type
    uint32_t type;

    // Size of the block following the header
    uint32_t insize;

    // If the type requires modifications, the resulting size after applying
    // them. For example, for a compressed image, outsize represents the size of
    // the final image after decompression.
    // If no modifications are required, this must be set to insize.
    uint32_t outsize;

    // Flags for the boot data. See flag descriptions for each type.
    uint32_t flags;
} bootdata_t;

__END_CDECLS;
