/* Copyright 2019 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FIRMWARE_LIBABR_ABR_OPS_H_
#define FIRMWARE_LIBABR_ABR_OPS_H_

#include "abr_sysdeps.h"

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
   * bytes are read. Setting this function to NULL will cause libabr to return I/O errors.
   */
  bool (*read_abr_metadata)(void* context, size_t size, uint8_t* buffer);

  /* Writes |size| bytes of A/B/R metadata from |buffer| to persistent storage.
   *
   * Returns true on success. This function must fail if fewer than |size| bytes are written.
   * Setting this function to NULL is appropriate in environments where metadata is read-only.
   */
  bool (*write_abr_metadata)(void* context, const uint8_t* buffer, size_t size);
} AbrOps;

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_LIBABR_ABR_OPS_H_ */
