/* Copyright 2019 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SRC_FIRMWARE_LIB_ABR_INCLUDE_LIB_ABR_OPS_H_
#define SRC_FIRMWARE_LIB_ABR_INCLUDE_LIB_ABR_OPS_H_

#include "data.h"
#include "sysdeps.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Operations called by libabr that require platform-dependent implementation. */
typedef struct AbrOps {
  /* Available for use by the AbrOps implementation. This is passed by libabr to every AbrOps call,
   * but libabr does not use this value. If this is not used by an implementation, it can be safely
   * set to NULL.
   */
  void* context;

  /* Reads |size| bytes of A/B/R metadata from persistent storage into |buffer|.
   *
   * On success, populates |buffer| and returns true. This function must fail if fewer than |size|
   * bytes are read.
   *
   * Either read_abr_metadata or read_abr_metadata_custom must be provided, and the other must be
   * NULL.
   */
  bool (*read_abr_metadata)(void* context, size_t size, uint8_t* buffer);

  /* Writes |size| bytes of A/B/R metadata from |buffer| to persistent storage.
   *
   * Returns true on success. This function must fail if fewer than |size| bytes are written.
   *
   * Either write_abr_metadata or write_abr_metadata_custom may be provided, but not both. In
   * read-only environments, they may both be NULL.
   */
  bool (*write_abr_metadata)(void* context, const uint8_t* buffer, size_t size);

  /* Reads ABR data into |a_slot_data|, |b_slot_data|, and |one_shot_recovery|.
   *
   * Returns true on success. This function must fail if any metadata fails to read.
   * The client is responsible for ensuring the integrity of the data.
   *
   * Either read_abr_metadata or read_abr_metadata_custom must be provided, and the other must be
   * NULL.
   */
  bool (*read_abr_metadata_custom)(void* context, AbrSlotData* a_slot_data,
                                   AbrSlotData* b_slot_data, uint8_t* one_shot_recovery);

  /* Writes ABR data from |a_slot_data|, |b_slot_data|, and |one_shot_recovery| to disk.
   *
   * Returns true on success. This function must fail if any metadata fails to write.
   * The client is responsible for ensuring the integrity of the data.
   *
   * Either write_abr_metadata or write_abr_metadata_custom may be provided, but not both. In
   * read-only environments, they may both be NULL.
   */
  bool (*write_abr_metadata_custom)(void* context, const AbrSlotData* a_slot_data,
                                    const AbrSlotData* b_slot_data, uint8_t one_shot_recovery);
} AbrOps;

#ifdef __cplusplus
}
#endif

#endif  // SRC_FIRMWARE_LIB_ABR_INCLUDE_LIB_ABR_OPS_H_
