// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <targetos.h>

void free_clear(void* alloc_ptr_addr);

// Cache Line-Aligned Allocation/Deallocation Routines
void* aalloc(size_t size);
void afree_clear(void* aaloc_ptr_addr);

/***********************************************************************/
/* CRC32 Related Definitions/Declaration                               */
/***********************************************************************/
extern const ui32 Crc32Tbl[256];
#define CRC32_START 0xFFFFFFFF // starting CRC bit string
#define CRC32_FINAL 0xDEBB20E3 // summed over data and CRC
#define CRC32_UPDATE(crc, c) ((crc >> 8) ^ Crc32Tbl[(ui8)(crc ^ c)])

#ifdef __cplusplus
}
#endif
