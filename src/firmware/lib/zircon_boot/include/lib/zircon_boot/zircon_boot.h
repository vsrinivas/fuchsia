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
// This should point to the zbi.h in the zbi library in firmware sdk..
#include <lib/zbi/zbi.h>
#include <lib/zircon_boot/zbi_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ZirconBootResult {
  kBootResultOK = 0,

  kBootResultErrorMarkUnbootable,
  kBootResultErrorReadHeader,
  kBootResultErrorZbiHeaderNotFound,
  kBootResultErrorReadImage,
  kBootResultErrorSlotFail,
  kBootResultErrorNoValidSlot,
  kBootResultErrorGetFirmwareSlot,
  kBootResultErrorMismatchedFirmwareSlot,
  kBootResultRebootReturn,
  kBootResultBootReturn,

  kBootResultErrorInvalidSlotIdx,
  kBootResultErrorImageTooLarge,

  kBootResultErrorAppendZbiItems,
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

  // Gets the A/B/R slot of the currrently running firmware. The method needs to be implemented
  // to support firmware A/B/R logic. Specifically, device needs to find a way to store and pass
  // this information from non-A/B/R bootloader, i.e. stashing in registers.
  //
  // LoadAndBoot() will check whether this pointer is NULL to decide whether to boot according to
  // OS ABR or firmware ABR.
  //
  // @ops: Pointer to the host |ZirconBootOps|
  // @out_slot: An output pointer for returning firmware slot
  //
  // Returns true on success.
  bool (*get_firmware_slot)(ZirconBootOps* ops, AbrSlotIndex* out_slot);

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
