/* Copyright 2019 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "abr_data.h"
#include "abr_ops.h"
#include "abr_sysdeps.h"
#include "abr_util.h"
#include "libabr.h"

/* Initializes |data| with default valid values. Both A/B slots will be marked as bootable, but not
 * successful, with a full set of retries. Slot A will be higher priority.
 */
static void abr_data_init(AbrData* data) {
  AbrMemset(data, 0, sizeof(*data));
  AbrMemcpy(data->magic, kAbrMagic, kAbrMagicLen);
  data->version_major = kAbrMajorVersion;
  data->version_minor = kAbrMinorVersion;
  data->slot_data[0].priority = kAbrMaxPriority;
  data->slot_data[0].tries_remaining = kAbrMaxTriesRemaining;
  data->slot_data[0].successful_boot = 0;
  data->slot_data[1].priority = kAbrMaxPriority - 1;
  data->slot_data[1].tries_remaining = kAbrMaxTriesRemaining;
  data->slot_data[1].successful_boot = 0;
}

/* Deserializes and validates |size| bytes from |buffer|. On success, |dest| is populated with the
 * valid data and kAbrResultOk is returned.
 */
static AbrResult abr_data_deserialize(const uint8_t* buffer, size_t size, AbrData* dest) {
  if (size < sizeof(*dest)) {
    ABR_ERROR("Wrong serialized data size.\n");
    return kAbrResultErrorInvalidData;
  }

  AbrMemcpy(dest, buffer, sizeof(*dest));

  /* Ensure magic is correct. */
  if (AbrSafeMemcmp(dest->magic, kAbrMagic, kAbrMagicLen) != 0) {
    ABR_ERROR("Magic is incorrect.\n");
    return kAbrResultErrorInvalidData;
  }

  /* Bail if CRC32 doesn't match. */
  dest->crc32 = AbrBigEndianToHost(dest->crc32);
  if (dest->crc32 != AbrCrc32(dest, sizeof(*dest) - sizeof(uint32_t))) {
    ABR_ERROR("CRC32 does not match.\n");
    return kAbrResultErrorInvalidData;
  }

  /* Ensure we don't attempt to access any fields if the major version is not supported. */
  if (dest->version_major > kAbrMajorVersion) {
    ABR_ERROR("No support for given major version.\n");
    return kAbrResultErrorUnsupportedVersion;
  }

  return kAbrResultOk;
}

/* Updates the checksum and serializes |src| to |dest| which must point to at least sizeof(AbrData)
 * bytes.
 */
static void abr_data_serialize(AbrData* src, uint8_t* dest) {
  src->crc32 = AbrHostToBigEndian(AbrCrc32(src, sizeof(*src) - sizeof(uint32_t)));
  AbrMemcpy(dest, src, sizeof(*src));
}

static bool is_slot_bootable(const AbrSlotData* slot) {
  return (slot->priority > 0) && (slot->successful_boot || (slot->tries_remaining > 0));
}

static void set_slot_unbootable(AbrSlotData* slot) {
  slot->priority = 0;
  slot->tries_remaining = 0;
  slot->successful_boot = 0;
}

static bool is_slot_active(const AbrData* abr_data, AbrSlotIndex slot_index) {
  AbrSlotIndex active_slot =
      (abr_data->slot_data[kAbrSlotIndexB].priority > abr_data->slot_data[kAbrSlotIndexA].priority)
          ? kAbrSlotIndexB
          : kAbrSlotIndexA;
  if (!is_slot_bootable(&abr_data->slot_data[active_slot])) {
    active_slot = kAbrSlotIndexR;
  }
  return (active_slot == slot_index);
}

/* Ensure all unbootable or invalid states are marked as the canonical 'unbootable' state. That is,
 * priority=0, tries_remaining=0, and successful_boot=0.
 */
static void slot_normalize(AbrSlotData* slot) {
  if (slot->priority > 0) {
    if ((slot->tries_remaining == 0) && !slot->successful_boot) {
      /* We've exhausted all tries -> unbootable. */
      set_slot_unbootable(slot);
    }
    if ((slot->tries_remaining > 0) && slot->successful_boot) {
      /* Illegal state - AbrMarkSlotSuccessful() will clear tries_remaining when setting
       * successful_boot. Reset to not successful state.
       */
      slot->tries_remaining = kAbrMaxTriesRemaining;
      slot->successful_boot = 0;
    }
    if (slot->priority > kAbrMaxPriority) {
      slot->priority = kAbrMaxPriority;
    }
    if (slot->tries_remaining > kAbrMaxTriesRemaining) {
      slot->tries_remaining = kAbrMaxTriesRemaining;
    }
  } else {
    set_slot_unbootable(slot);
  }
}

/* Saves |abr_data| to persistent storage, overwriting any existing persistent state. */
static AbrResult save_metadata(const AbrOps* abr_ops, AbrData* abr_data) {
  uint8_t serialized[sizeof(*abr_data)];

  ABR_DEBUG("Writing A/B metadata to disk.\n");

  abr_data_serialize(abr_data, serialized);

  if (abr_ops->write_abr_metadata == NULL) {
    ABR_ERROR("Failed to write metadata (not implemented).\n");
    return kAbrResultErrorIo;
  }

  if (!abr_ops->write_abr_metadata(abr_ops->context, serialized, sizeof(serialized))) {
    ABR_ERROR("Failed to write metadata.\n");
    return kAbrResultErrorIo;
  }

  return kAbrResultOk;
}

/* Loads |abr_data| from persistent storage and normalizes it, initializing new data if necessary.
 * Changes as a result of normalization are not written back to persistent storage but a copy of the
 * exact original data from persistent storage is provided in |abr_data_orig| for future use with
 * save_metadata_if_changed().
 *
 * On success, populates abr_data, abr_data_orig, and returns kAbrResultOk. On failure an
 * ABR_RESULT_ERROR* error code is returned and the contents of abr_data and abr_data_orig are
 * undefined.
 */
static AbrResult load_metadata(const AbrOps* abr_ops, AbrData* abr_data, AbrData* abr_data_orig) {
  AbrResult result = kAbrResultOk;
  uint8_t serialized[sizeof(*abr_data)];

  if (abr_ops->read_abr_metadata == NULL) {
    ABR_ERROR("Failed to read metadata (not implemented).\n");
    return kAbrResultErrorIo;
  }

  if (!abr_ops->read_abr_metadata(abr_ops->context, sizeof(serialized), serialized)) {
    ABR_ERROR("Failed to read metadata.\n");
    return kAbrResultErrorIo;
  }

  result = abr_data_deserialize(serialized, sizeof(serialized), abr_data);
  if (result == kAbrResultErrorUnsupportedVersion) {
    /* We don't want to clobber valid data in persistent storage, but we can't use this data, so
     * bail out.
     */
    return result;
  } else if (result != kAbrResultOk) {
    /* No valid data exists. Use default data and set original data to trigger update. */
    abr_data_init(abr_data);
    AbrMemset(abr_data_orig, 0, sizeof(*abr_data_orig));
    return kAbrResultOk;
  }

  *abr_data_orig = *abr_data;
  slot_normalize(&abr_data->slot_data[kAbrSlotIndexA]);
  slot_normalize(&abr_data->slot_data[kAbrSlotIndexB]);
  return kAbrResultOk;
}

/* Writes metadata to disk only if it has changed. |abr_data_orig| should be from load_metadata().
 */
static AbrResult save_metadata_if_changed(const AbrOps* abr_ops, AbrData* abr_data,
                                          AbrData* abr_data_orig) {
  if (AbrSafeMemcmp(abr_data, abr_data_orig, sizeof(*abr_data)) == 0) {
    return kAbrResultOk;
  }

  return save_metadata(abr_ops, abr_data);
}

static bool check_slot_index(AbrSlotIndex slot_index) {
  if (slot_index < kAbrSlotIndexA || slot_index > kAbrSlotIndexR) {
    ABR_ERROR("Invalid slot index.\n");
    return false;
  }
  return true;
}

AbrSlotIndex AbrGetBootSlot(const AbrOps* abr_ops, bool update_metadata,
                            bool* is_slot_marked_successful) {
  AbrData abr_data, abr_data_orig;
  AbrResult result = kAbrResultOk;
  AbrSlotIndex slot_to_boot = kAbrSlotIndexR;
  if (is_slot_marked_successful)
    *is_slot_marked_successful = false;

  result = load_metadata(abr_ops, &abr_data, &abr_data_orig);
  if (result != kAbrResultOk) {
    ABR_ERROR("Failed to load metadata, falling back to recovery mode.\n");
    return kAbrSlotIndexR;
  }

  /* One-shot recovery boot has the highest priority if metadata can be updated. */
  if (abr_data.one_shot_recovery_boot && update_metadata) {
    abr_data.one_shot_recovery_boot = 0;
    if (save_metadata(abr_ops, &abr_data) == kAbrResultOk) {
      return kAbrSlotIndexR;
    }
    ABR_ERROR("Failed to update one-shot state. Ignoring one-shot request.\n");
    /* Put it back how it was, maybe a later boot stage will be able to handle it. */
    abr_data.one_shot_recovery_boot = 1;
  }

  /* Choose the highest priority slot. This logic assumes normalized metadata. */
  slot_to_boot =
      (abr_data.slot_data[kAbrSlotIndexB].priority > abr_data.slot_data[kAbrSlotIndexA].priority)
          ? kAbrSlotIndexB
          : kAbrSlotIndexA;
  if (is_slot_bootable(&abr_data.slot_data[slot_to_boot])) {
    if (is_slot_marked_successful && abr_data.slot_data[slot_to_boot].successful_boot) {
      *is_slot_marked_successful = true;
    }
  } else {
    ABR_DEBUG("All slots are unbootable, falling back to recovery mode.\n");
    slot_to_boot = kAbrSlotIndexR;
  }

  if (update_metadata) {
    /* In addition to any changes that resulted from normalization, there is one change to be made
     * here and that is to decrement the tries remaining for a slot not yet marked as successful.
     */
    if ((slot_to_boot != kAbrSlotIndexR) && !abr_data.slot_data[slot_to_boot].successful_boot) {
      abr_data.slot_data[slot_to_boot].tries_remaining--;
    }
    result = save_metadata_if_changed(abr_ops, &abr_data, &abr_data_orig);
    if (result != kAbrResultOk) {
      /* We have no choice but to proceed without updating metadata. */
      ABR_ERROR("Failed to update metadata, proceeding anyways.\n");
    }
  }

  return slot_to_boot;
}

const char* AbrGetSlotSuffix(AbrSlotIndex slot_index) {
  static const char* slot_suffixes[] = {"_a", "_b", "_r"};
  if (!check_slot_index(slot_index)) {
    return "";
  }
  return slot_suffixes[slot_index];
}

AbrResult AbrMarkSlotActive(const AbrOps* abr_ops, AbrSlotIndex slot_index) {
  AbrData abr_data, abr_data_orig;
  AbrSlotIndex other_slot_index;
  AbrResult result;

  if (!check_slot_index(slot_index)) {
    return kAbrResultErrorInvalidData;
  }

  if (slot_index == kAbrSlotIndexR) {
    ABR_ERROR("Invalid argument: Cannot mark slot R as active.\n");
    return kAbrResultErrorInvalidData;
  }

  result = load_metadata(abr_ops, &abr_data, &abr_data_orig);
  if (result != kAbrResultOk) {
    return result;
  }

  /* Make requested slot top priority, unsuccessful, and with max tries. */
  abr_data.slot_data[slot_index].priority = kAbrMaxPriority;
  abr_data.slot_data[slot_index].tries_remaining = kAbrMaxTriesRemaining;
  abr_data.slot_data[slot_index].successful_boot = 0;

  /* Ensure other slot doesn't have as high a priority. */
  other_slot_index = 1 - slot_index;
  if (abr_data.slot_data[other_slot_index].priority == kAbrMaxPriority) {
    abr_data.slot_data[other_slot_index].priority--;
  }

  return save_metadata_if_changed(abr_ops, &abr_data, &abr_data_orig);
}

AbrResult AbrMarkSlotUnbootable(const AbrOps* abr_ops, AbrSlotIndex slot_index) {
  AbrData abr_data, abr_data_orig;
  AbrResult result;

  if (!check_slot_index(slot_index)) {
    return kAbrResultErrorInvalidData;
  }

  if (slot_index == kAbrSlotIndexR) {
    ABR_ERROR("Invalid argument: Cannot mark slot R as unbootable.\n");
    return kAbrResultErrorInvalidData;
  }

  result = load_metadata(abr_ops, &abr_data, &abr_data_orig);
  if (result != kAbrResultOk) {
    return result;
  }

  set_slot_unbootable(&abr_data.slot_data[slot_index]);

  return save_metadata_if_changed(abr_ops, &abr_data, &abr_data_orig);
}

AbrResult AbrMarkSlotSuccessful(const AbrOps* abr_ops, AbrSlotIndex slot_index) {
  AbrData abr_data, abr_data_orig;
  AbrResult result;

  if (!check_slot_index(slot_index)) {
    return kAbrResultErrorInvalidData;
  }

  if (slot_index == kAbrSlotIndexR) {
    ABR_ERROR("Invalid argument: Cannot mark slot R as successful.\n");
    return kAbrResultErrorInvalidData;
  }

  result = load_metadata(abr_ops, &abr_data, &abr_data_orig);
  if (result != kAbrResultOk) {
    return result;
  }

  if (!is_slot_bootable(&abr_data.slot_data[slot_index])) {
    ABR_ERROR("Invalid argument: Cannot mark unbootable slot as successful.\n");
    return kAbrResultErrorInvalidData;
  }

  abr_data.slot_data[slot_index].tries_remaining = 0;
  abr_data.slot_data[slot_index].successful_boot = 1;

  return save_metadata_if_changed(abr_ops, &abr_data, &abr_data_orig);
}

AbrResult AbrGetSlotInfo(const AbrOps* abr_ops, AbrSlotIndex slot_index, AbrSlotInfo* info) {
  AbrData abr_data, abr_data_orig;
  AbrResult result;

  if (info == NULL) {
    ABR_ERROR("Invalid argument: |info| cannot be NULL.\n");
    return kAbrResultErrorInvalidData;
  }

  AbrMemset(info, 0, sizeof(*info));

  if (!check_slot_index(slot_index)) {
    return kAbrResultErrorInvalidData;
  }

  result = load_metadata(abr_ops, &abr_data, &abr_data_orig);
  if (result != kAbrResultOk) {
    return result;
  }

  if (slot_index == kAbrSlotIndexR) {
    /* Assume that R slot is always OK. */
    info->is_bootable = true;
    info->is_active = is_slot_active(&abr_data, kAbrSlotIndexR);
    info->is_marked_successful = true;
    info->num_tries_remaining = 0;
    return kAbrResultOk;
  }

  info->is_bootable = is_slot_bootable(&abr_data.slot_data[slot_index]);
  info->is_active = is_slot_active(&abr_data, slot_index);
  info->is_marked_successful = abr_data.slot_data[slot_index].successful_boot;
  info->num_tries_remaining = abr_data.slot_data[slot_index].tries_remaining;

  return kAbrResultOk;
}

AbrResult AbrSetOneShotRecovery(const AbrOps* abr_ops, bool enable) {
  AbrData abr_data, abr_data_orig;
  AbrResult result;

  result = load_metadata(abr_ops, &abr_data, &abr_data_orig);
  if (result != kAbrResultOk) {
    return result;
  }

  abr_data.one_shot_recovery_boot = enable ? 1 : 0;

  return save_metadata_if_changed(abr_ops, &abr_data, &abr_data_orig);
}
