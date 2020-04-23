// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The header defines structures and functions for supporting header-based
// reconfigurable sysconfig partition layout. The logic are writtent in C
// because bootloader needs to use it as well.
#ifndef LIB_SYSCONFIG_SYSCONFIG_HEADER_H_
#define LIB_SYSCONFIG_SYSCONFIG_HEADER_H_

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SYSCONFIG_HEADER_MAGIC_ARRAY \
  { 'S', 'C', 'F', 'G' }
#define SYSCONFIG_HEADER_MAGIC_STR "SCFG"

#if defined(SYSCONFIG_HEADER_DEBUG)
#define syshdrP(fmt...) printf("[sysconfig-header]%s:%d:", __func__, __LINE__), printf(fmt)
#else
#define syshdrP(fmt...)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// The following structures model header based sysconfig partition layout.
struct sysconfig_subpartition {
  uint64_t offset;
  uint64_t size;
} __attribute__((packed));

struct sysconfig_header {
  uint8_t magic[4];
  uint8_t reserved[4];
  struct sysconfig_subpartition sysconfig_data;
  struct sysconfig_subpartition abr_metadata;
  struct sysconfig_subpartition vb_metadata_a;
  struct sysconfig_subpartition vb_metadata_b;
  struct sysconfig_subpartition vb_metadata_r;
  uint32_t crc_value;
} __attribute__((packed));

_Static_assert(sizeof(struct sysconfig_header) == 92, "Unexpected size of sysconfig_header.");

#define SYSCONFIG_HEADER_SIZE sizeof(struct sysconfig_header)

/**
 * Check whether two sysconfig_header, represented by <lhs> and <rhs> are
 * equal. It will not take into consideration of <crc_value> field.
 *
 * Return true if equal, false if not equal
 */
bool sysconfig_header_equal(const struct sysconfig_header *lhs, const struct sysconfig_header *rhs);

/**
 * Forward declaration of crc32 function. It is provided by the system and
 * will be used in update_sysconfig_header_magic_and_crc.
 *
 */
uint32_t sysconfig_header_crc32(uint32_t crc, const uint8_t *buf, size_t len);

/**
 * Check whether a given sysconfig_header is valid w.r.t the page size and
 * total sysconfig partition size. Conditions to check include:
 * 1. valid magic.
 * 2. valid crc.
 * 3. sub-partition fits into partition.
 * 4. sub-partitions do not overlap.
 * 5. the first page is reserved for header.
 *
 * Return true if valid, false if not valid.
 */
bool sysconfig_header_valid(const struct sysconfig_header *header, uint64_t page_size,
                            uint64_t partition_size);

/**
 * Compute and set the crc_value field and the magic array of the given header.
 *
 */
void update_sysconfig_header_magic_and_crc(struct sysconfig_header *header);

#ifdef __cplusplus
}
#endif

#endif  // LIB_SYSCONFIG_SYSCONFIG_HEADER_H_
