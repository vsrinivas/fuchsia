// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <targetos.h>

/***********************************************************************/
/* Symbol Definitions                                                  */
/***********************************************************************/
#define VERBOSE 1

/***********************************************************************/
/* Definitions related to reading/writing NVRAM memory.                */
/***********************************************************************/
int NvRead(const char* name, void* data, int type);
int NvReadBin(const char* name, void* data, int len);
int NvReadStr(const char* name, void* data, int maxlen);
int NvWrite(const char* name, const void* data, int type);
int NvWriteBin(const char* name, const void* data, int len);
int NvReadBinSize(const char* name);
#define NV_BYTE 0
#define NV_SHORT 1
#define NV_LONG 2
#define NV_STRING 3
#define NV_IP_ADDR 4
#define NV_ETH_ADDR 5
#define NV_ERR_LOG 6
#define NV_IP6_ADDR 7
#define NV_BINARY 8
int NvDelete(const char* name, int type);
void NvSave(void);

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
