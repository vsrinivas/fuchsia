// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>    // For fixed width types.

#ifdef __cplusplus
extern "C" {
#endif

//
// Type Declarations.
//

// NDM Control Block
typedef struct ndm* NDM;
typedef const struct ndm* CNDM;

// FTL Interface Structure
typedef struct XfsVol {
    // Driver functions
    int (*write_pages)(const void* buf, uint32_t page0, int cnt, void* vol);
    int (*read_pages)(void* buf, uint32_t page0, int cnt, void* vol);
    int (*report)(void *vol, uint32_t msg, ...);

    const char* name;       // volume name
    uint32_t flags;         // option flags
    uint32_t num_pages;     // number of pages in volume
    uint32_t page_size;     // page size in bytes
    void* vol;              // driver's volume pointer
    void* ftl_volume;       // ftl layer (block device) volume
} XfsVol;

#ifdef __cplusplus
}
#endif

