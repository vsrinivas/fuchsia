/* Copyright 2019 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This is the main library header file, and the only header file callers need
 * to include directly. Most callers only need to call AbrGetBootSlot().
 */

#ifndef FIRMWARE_LIBABR_LIBABR_H_
#define FIRMWARE_LIBABR_LIBABR_H_

#include "abr_ops.h"
#include "abr_sysdeps.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  kAbrResultOk,
  kAbrResultErrorIo,
  kAbrResultErrorInvalidData,
  kAbrResultErrorUnsupportedVersion,
} AbrResult;

typedef enum {
  kAbrSlotIndexA,
  kAbrSlotIndexB,
  kAbrSlotIndexR,
} AbrSlotIndex;

/* This structure describes the current state of an A/B slot.
 *
 * Note that slot R does not have associated metadata and is always considered bootable and
 * successful. It is only considered active when no other slots are bootable.
 *
 * When metadata is uninitialized, it will be initialized to a state which allows a full set of
 * tries for each slot with slot A as highest priority.
 *
 * Fields:
 *    is_bootable - Whether the slot is expected to be bootable.
 *    is_active - Whether the slot is the highest priority bootable slot. This is not a predictor of
 *                AbrGetBootSlot(), which will account for additional configuration like one-shot
 *                recovery requests.
 *    is_marked_successful - Whether the slot has been marked as having booted successfully since
 *                           the last update.
 *    num_tries_remaining - The number of tries remaining to attempt a successful boot. If this
 *                          reaches zero and a slot has not been marked successful, the slot is
 *                          considered unbootable. This value is only meaningful if |is_bootable| is
 *                          true and |is_marked_successful| is false.
 */
typedef struct {
  bool is_bootable;
  bool is_active;
  bool is_marked_successful;
  uint8_t num_tries_remaining;
} AbrSlotInfo;

/* This function implements the core A/B/R logic. It selects a slot to boot based on the current
 * state of the A/B/R metadata. The following algorithm is used:
 *    - If one-shot recovery has been requested and |update_metadata| is true, or if no bootable A/B
 *      slots exist, choose slot R.
 *    - If at least one valid slot exists, choose the valid slot with the highest priority (that is,
 *      the active slot).
 *
 * When |update_metadata| is true, this function may update the stored metadata in the following
 * ways:
 *    - The retry counter will be modified if a slot is chosen which has not been marked successful.
 *    - The one-shot recovery field will be reset if it is handled.
 *    - Invalid metadata will be overwritten with default valid metadata.
 *
 * Parameters:
 *    abr_ops - A populated instance of AbrOps. If |update_metadata| is false, then the
 *              |write_abr_metadata| op may be NULL.
 *    update_metadata - Whether metadata should be updated. This may be set to false early in boot
 *                      when metadata storage is read-only. If set to false, requests for one-shot
 *                      recovery are ignored because there is no way to reset the request. In cases
 *                      where A/B/R logic is run by multiple layers during boot, the one-shot
 *                      recovery request will be handled by the first layer that is capable of
 *                      handling it.
 *    is_slot_marked_successful - On success, is populated with a boolean which indicates whether
 *                                the chosen slot has been marked as successful. This is provided
 *                                for convenience and can be set to NULL.
 */
AbrSlotIndex AbrGetBootSlot(const AbrOps* abr_ops, bool update_metadata,
                            bool* is_slot_marked_successful);

/* A convenience function which provides the partition label suffix associated with a given
 * |slot_index|. For example, "_a" for kAbrSlotIndexA. If an invalid |slot_index| is provided, the
 * empty string is returned.
 */
const char* AbrGetSlotSuffix(AbrSlotIndex slot_index);

/* Marks the given |slot_index| as active. Returns kAbrResultOk on success.
 *
 * Calling this on kAbrSlotIndexR is an error and kAbrResultErrorInvalidData will be returned.
 *
 * This function is typically used by the OS update system when completing an update. It is not
 * normally used by a bootloader except in response to an explicit operator command.
 *
 * Even though the active slot is just the highest priority bootable slot, marking a slot as active
 * does more than change the priority. Specifically, this function will:
 *    - Mark the slot as bootable with highest priority, reducing the priority of other slots if
 *      necessary.
 *    - Mark the slot as NOT successful.
 *    - Set the tries remaining to the max value.
 */
AbrResult AbrMarkSlotActive(const AbrOps* abr_ops, AbrSlotIndex slot_index);

/* Marks the given |slot_index| as unbootable. Returns kAbrResultOk on success.
 *
 * Calling this on kAbrSlotIndexR is an error and kAbrResultErrorInvalidData will be returned.
 *
 * This function is typically used by the OS update system before writing to a slot.
 */
AbrResult AbrMarkSlotUnbootable(const AbrOps* abr_ops, AbrSlotIndex slot_index);

/* Marks the given |slot_index| as successful. Returns kAbrResultOk on success.
 *
 * Calling this on an unbootable slot is an error and kAbrResultErrorInvalidData will be
 * returned.
 *
 * Calling this on kAbrSlotIndexR is an error and kAbrResultErrorInvalidData will be returned.
 *
 * This function is typically used by the OS update system after having confirmed that the slot
 * works as intended. It is not normally used by a bootloader except in response to an explicit
 * operator command.
 */
AbrResult AbrMarkSlotSuccessful(const AbrOps* abr_ops, AbrSlotIndex slot_index);

/* Gets the current |info| for |slot_index|. On success populates |info| and returns kAbrResultOk.
 */
AbrResult AbrGetSlotInfo(const AbrOps* abr_ops, AbrSlotIndex slot_index, AbrSlotInfo* info);

/* Updates metadata to enable or disable one-shot recovery.
 *
 * This function is typically used by an OS to force recovery even when another bootable slot
 * exists. When AbrGetBootSlot responds to this setting, it also resets the setting so recovery
 * will only be triggered once. If AbrGetBootSlot is called with |update_metadata| set to false,
 * or is otherwise unable to reset the setting, the setting will be ignored.
 */
AbrResult AbrSetOneShotRecovery(const AbrOps* abr_ops, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_LIBABR_LIBABR_H_ */
