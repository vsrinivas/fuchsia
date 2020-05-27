// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The header file defines structures and functions for supporting abr wear-leveling
// algorithm. The logic are written in C as it needs to be used in both OS
// and bootloader.

#ifndef LIB_SYSCONFIG_ABR_WEAR_LEVELING_H_
#define LIB_SYSCONFIG_ABR_WEAR_LEVELING_H_

#include "sysconfig-header.h"

#define ABR_WEAR_LEVELING_ABR_DATA_SIZE 32
#define ABR_WEAR_LEVELING_MAGIC_OFFSET ABR_WEAR_LEVELING_ABR_DATA_SIZE
#define ABR_WEAR_LEVELING_MAGIC_LEN 4
#define ABR_WEAR_LEVELING_MAGIC_BYTE_0 0xaa
#define ABR_WEAR_LEVELING_MAGIC_BYTE_1 0x55
#define ABR_WEAR_LEVELING_MAGIC_BYTE_2 0x11
#define ABR_WEAR_LEVELING_MAGIC_BYTE_3 0x22

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Abr metadata extended with a magic for wear-leveling.
 * Though abr metadata itself comes with a magic. Here we use a separate magic
 * to avoid exposing internal detail of abr logic to wear-leveling algorithm.
 */
struct abr_metadata_ext {
  uint8_t abr_data[ABR_WEAR_LEVELING_ABR_DATA_SIZE];
  uint8_t magic[ABR_WEAR_LEVELING_MAGIC_LEN];
} __attribute__((packed));

/**
 * Checks whether an abr metadata page is valid in the context of wear-levelingn
 * It simply checks the magic in abr_metadata_ext.
 *
 * Return true if valid, false otherwise.
 */
bool abr_metadata_page_valid(const struct abr_metadata_ext *abr_data);

/**
 * Checks whether the sysconfig layout specified by <header> supports abr wear-leveling.
 *
 * Returns true if it supports, false otherwise.
 */
bool layout_support_wear_leveling(const struct sysconfig_header *header, size_t page_size);

/**
 * Finds the latest abr metadata in abr sub-partition given as a memory buffer.
 * Copies the new data into <out>. The function will check the magic in each page.
 * If no page contains valid magic, it copies from the first page.
 *
 */
void find_latest_abr_metadata_page(const struct sysconfig_header *header, const void *abr_subpart,
                                   uint64_t page_size, struct abr_metadata_ext *out);
/**
 * Finds an valid empty page for appending new abr metadata.
 * The requirement of consecutive page programming is met by trying to find the
 * immediate empty page after the last non-empty page in the sub-partition.
 *
 * Returns true if there is an empty page to write. The page index will be assigned
 * to <out>. Returns false otherwise.
 */
bool find_empty_page_for_wear_leveling(const struct sysconfig_header *header,
                                       const uint8_t *abr_subpart, uint64_t page_size,
                                       int64_t *out);

/**
 * Set the magic field of the given abr_metadata_ext struct.
 *
 */
void set_abr_metadata_ext_magic(struct abr_metadata_ext *data);

#ifdef __cplusplus
}
#endif

#endif  // LIB_SYSCONFIG_ABR_WEAR_LEVELING_H_
