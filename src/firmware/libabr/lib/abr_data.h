/* Copyright 2019 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FIRMWARE_LIBABR_ABR_DATA_H_
#define FIRMWARE_LIBABR_ABR_DATA_H_

#include "abr_sysdeps.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ABR data structures have backward compatibility with the deprecated libavb_ab:
 *   https://android.googlesource.com/platform/external/avb/+/37f5946d0e1159273eff61dd8041377fedbf55a9/libavb_ab/
 */

/* Magic for the A/B struct when serialized. */
static const char* kAbrMagic = "\0AB0";
static const size_t kAbrMagicLen = 4;

/* Versioning for the on-disk A/B metadata. */
static const uint8_t kAbrMajorVersion = 2;
static const uint8_t kAbrMinorVersion = 0;

/* Maximum values for slot data. */
static const uint8_t kAbrMaxPriority = 15;
static const uint8_t kAbrMaxTriesRemaining = 7;

/* Struct used for recording per-slot metadata. */
typedef struct AbrSlotData {
  /* Slot priority. Valid values range from 0 to kAbrMaxPriority, both inclusive with 1 being the
   * lowest and kAbrMaxPriority being the highest. The special value 0 is used to indicate the slot
   * is unbootable.
   */
  uint8_t priority;

  /* Number of times left attempting to boot this slot ranging from 0 to kAbrMaxTriesRemaining. */
  uint8_t tries_remaining;

  /* Non-zero if this slot has booted successfully. */
  uint8_t successful_boot;

  /* Reserved for future use. */
  uint8_t reserved[1];
} ABR_ATTR_PACKED AbrSlotData;

/* Struct used for recording A/B/R metadata.
 *
 * When serialized, data is stored in network byte-order.
 */
typedef struct AbrData {
  /* Magic number used for identification - see kAbrMagic. */
  uint8_t magic[kAbrMagicLen];

  /* Version of on-disk struct - see ABR_{MAJOR,MINOR}_VERSION. */
  uint8_t version_major;
  uint8_t version_minor;

  /* Reserved for future use. */
  uint8_t reserved1[2];

  /* A/B per-slot metadata. Recovery boot does not have its own data and will be used if both A/B
   * slots are not bootable.
   */
  AbrSlotData slot_data[2];

  /* One-shot force recovery boot. Non-zero if one-shot recovery requested. */
  uint8_t one_shot_recovery_boot;

  /* Reserved for future use. */
  uint8_t reserved2[11];

  /* CRC32 of all 28 bytes preceding this field. */
  uint32_t crc32;
} ABR_ATTR_PACKED AbrData;

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_LIBABR_ABR_DATA_H_ */
