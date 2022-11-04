// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_LIB_ZIRCON_BOOT_INCLUDE_LIB_ZIRCON_BOOT_ZIRCON_BOOT_H_
#define SRC_FIRMWARE_LIB_ZIRCON_BOOT_INCLUDE_LIB_ZIRCON_BOOT_ZIRCON_BOOT_H_

// Standard library may not be available for all platforms. If that's the case,
// device should provide their own header that implements the equivalents.
//
// The library uses ABR library in this sdk, where there is also a switch to
// decide whether to use standard library. It should be adjusted accordingly as well.
#ifdef ZIRCON_BOOT_CUSTOM_SYSDEPS_HEADER
#include <zircon_boot_sysdeps.h>
#else
#include <stddef.h>
#endif

// This should point to the abr.h in the abr library in firmware sdk.
#include <lib/abr/abr.h>
// This should point to the zbi.h in the zbi library in firmware sdk.
#include <lib/zbi/zbi.h>
#include <lib/zircon_boot/zbi_utils.h>

#include <libavb_atx/libavb_atx.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ZirconBootResult {
  kBootResultOK = 0,

  kBootResultErrorInvalidArguments,
  kBootResultErrorMarkUnbootable,
  kBootResultErrorReadHeader,
  kBootResultErrorZbiHeaderNotFound,
  kBootResultErrorReadImage,
  kBootResultErrorSlotFail,
  kBootResultErrorNoValidSlot,
  kBootResultErrorIsSlotSupprotedByFirmware,
  kBootResultErrorMismatchedFirmwareSlot,
  kBootResultRebootReturn,
  kBootResultBootReturn,

  kBootResultErrorInvalidSlotIdx,
  kBootResultErrorImageTooLarge,

  kBootResultErrorAppendZbiItems,
  kBootResultErrorSlotVerification,
} ZirconBootResult;

struct ZirconBootOps;
typedef struct ZirconBootOps ZirconBootOps;

// Firmware specific operations required to use the library.
struct ZirconBootOps {
  // Device-specific data.
  void* context;

  // Reads from a partition.
  //
  // @ops: Pointer to the host |ZirconBootOps|.
  // @part: Name of the partition.
  // @offset: Offset in the partition to read from.
  // @size: Number of bytes to read.
  // @dst: Output buffer.
  // @read_size: Output pointer for storing the actual number of bytes read. The libary uses
  // this value to verify the integrity of read data. It expects it to always equal the
  // requested number of bytes.
  //
  // Returns true on success.
  bool (*read_from_partition)(ZirconBootOps* ops, const char* part, size_t offset, size_t size,
                              void* dst, size_t* read_size);

  // Writes data to partition.
  //
  // @ops: Pointer to the host |ZirconBootOps|.
  // @part: Name of the partition.
  // @offset: Offset in the partition to write to.
  // @size: Number of bytes to write.
  // @src: Payload buffer.
  // @write_size: Output pointer for storing the actual number of bytes written. The library
  // uses this value to verify the integrity of write data. It expects it to always equal the
  // number of bytes requested to be written.
  //
  // Returns true on success.
  bool (*write_to_partition)(ZirconBootOps* ops, const char* part, size_t offset, size_t size,
                             const void* src, size_t* write_size);

  // Boots image in memory.
  //
  // @ops: Pointer to the host |ZirconBootOps|.
  // @image: Pointer to the zircon kernel image in memory. It is expected to be a zbi
  //        container.
  // @capacity: Capacity of the container.
  // @slot: The A/B/R slot index of the image.
  //
  // The function is not expected to return if boot is successful.
  void (*boot)(ZirconBootOps* ops, zbi_header_t* image, size_t capacity, AbrSlotIndex slot);

  // Checks whether the currently running firmware can be used to boot the target kernel slot.
  // If set, zircon_boot will call this before attempting to load/boot/decrease retry counter for
  // the current active slot. If |out| is true, it proceeds. Otherwise, it will calls the reboot
  // method below to trigger a device reboot. The typical use case of this method is to extend
  // A/B/R booting to firmware (firmware ABR booting). Specifically, user can check whether current
  // firmware slot is compatible with the active kernel slot to boot. If the same, boot can proceeds
  // Otherwise the library will trigger a reboot and expect that the device can reboot into a
  // firmware slot that is compatible with the active slot according to current abr metadata, i.e
  // the slot expected to be returned by AbrGetBootSlot(). The typical approach is to have earlier
  // stage firmware also boot according to A/B/R. Boards without firmware A/B/R can just leave this
  // function unimplemented.
  //
  // @ops: Pointer to the host |ZirconBootOps|
  // @kernel_slot: Target slot for kernel.
  // @out: An output pointer for returning whether current slot can boot the target `kernel_slot`
  //
  // Returns true on success, false otherwise.
  bool (*firmware_can_boot_kernel_slot)(ZirconBootOps* ops, AbrSlotIndex kernel_slot, bool* out);

  // Reboots the device.
  //
  // @ops: Pointer to the host |ZirconBootOps|
  // @force_recovery: Enable/Disable force recovery.
  //
  // The function is not expected to return if reboot is successful.
  void (*reboot)(ZirconBootOps* ops, bool force_recovery);

  // Adds device-specific ZBI items based on available boot information. The method is optional and
  // may be set to NULL, in which case no zbi items will be appended to the boot image.
  //
  // @ops: Pointer to the host |ZirconBootOps|
  // @image: The loaded kernel image as a ZBI container. Items should be appended to it.
  // @capacity: Capacity of the ZBI container.
  // @slot: A/B/R slot of the loaded image.
  //
  // Returns true on success.
  bool (*add_zbi_items)(ZirconBootOps* ops, zbi_header_t* image, size_t capacity,
                        AbrSlotIndex slot);

  // Following are operations required to perform zircon verified boot.
  // Verified boot implemented in this library is based on libavb. The library use the following
  // provided operations to interact with libavb for kernel verification.
  // If any of the following function pointers is set to NULL, verified boot is bypassed.

  // Gets the size of a partition with the name in |part|
  // (NUL-terminated UTF-8 string). Returns the value in
  // |out|.
  //
  // @ops: Pointer to the ZirconBootOps that host this ZirconVBootOps object.
  // @part: Name of the partition.
  // @out: Output pointer storing the result.
  //
  // Returns true on success.
  bool (*verified_boot_get_partition_size)(ZirconBootOps* ops, const char* part, size_t* out);

  // Gets the rollback index corresponding to the location given by
  // |rollback_index_location|. The value is returned in
  // |out_rollback_index|.
  //
  // @ops: Pointer to the ZirconBootOps that host this ZirconVBootOps object.
  // @rollback_index_location: Location to write rollback index.
  // @out_rollback_index: Output pointer for storing the index value.
  //
  // A device may have a limited amount of rollback index locations (say,
  // one or four) so may error out if |rollback_index_location| exceeds
  // this number.
  //
  // Returns true on success.
  bool (*verified_boot_read_rollback_index)(ZirconBootOps* ops, size_t rollback_index_location,
                                            uint64_t* out_rollback_index);

  // Sets the rollback index corresponding to the location given by
  // |rollback_index_location| to |rollback_index|.
  //
  // @ops: Pointer to the ZirconBootOps that host this ZirconVBootOps object.
  // @rollback_index_location: Location to rollback index to write.
  // @rollback_index: Value of the rollback index to write.
  //
  // A device may have a limited amount of rollback index locations (say,
  // one or four) so may error out if |rollback_index_location| exceeds
  // this number.
  //
  // Returns true on success.
  bool (*verified_boot_write_rollback_index)(ZirconBootOps* ops, size_t rollback_index_location,
                                             uint64_t rollback_index);

  // Gets whether the device is locked. The value is returned in
  // |out_is_locked| (true if locked, false otherwise).
  //
  // @ops: Pointer to the ZirconBootOps that host this ZirconVBootOps object.
  // @out_is_locked: Output pointer for storing the status.
  //
  // Returns true on success.
  bool (*verified_boot_read_is_device_locked)(ZirconBootOps* ops, bool* out_is_locked);

  // Reads permanent |attributes| data. There are no restrictions on where this
  // data is stored.
  //
  // @ops: Pointer to the ZirconBootOps that host this ZirconVBootOps object.
  // @attribute: Output pointer for storing the permanent attribute.
  //
  // Returns true on success.
  bool (*verified_boot_read_permanent_attributes)(ZirconBootOps* ops,
                                                  AvbAtxPermanentAttributes* attribute);

  // Reads a |hash| of permanent attributes. This hash MUST be retrieved from a
  // permanently read-only location (e.g. fuses) when a device is LOCKED.
  //
  // @ops: Pointer to the ZirconBootOps that host this ZirconVBootOps object.
  // @hash: Output buffer that stores the hash values. The buffer must be able to hold
  //        |AVB_SHA256_DIGEST_SIZE| bytes.
  //
  // Returns true on success.
  bool (*verified_boot_read_permanent_attributes_hash)(ZirconBootOps* ops, uint8_t* hash);
};

typedef enum ForceRecovery {
  kForceRecoveryOn,
  kForceRecoveryOff,
} ForceRecovery;

// Loads kernel image into memory and boots it. if ops.get_firmware_slot is set, the function
// boots according to firwmare ABR. Otherwise it boots according to OS ABR.
//
// @ops: Required operations.
// @load_address: Pointer to the target memory to load image.
// @load_address_size: Maximum size of the memory for loading the image.
// @force_recovery: Enable/Disable force recovery.
//
// The function is not expected to return if boot is successful.
ZirconBootResult LoadAndBoot(ZirconBootOps* ops, void* load_address, size_t load_address_size,
                             ForceRecovery force_recovery);

// Create operations for libabr from a ZirconBootOps.
AbrOps GetAbrOpsFromZirconBootOps(ZirconBootOps* ops);

// Returns the zircon partition name of a given slot.
const char* GetSlotPartitionName(AbrSlotIndex slot);

#ifdef __cplusplus
}
#endif

#endif  // SRC_FIRMWARE_LIB_ZIRCON_BOOT_INCLUDE_LIB_ZIRCON_BOOT_ZIRCON_BOOT_H_
