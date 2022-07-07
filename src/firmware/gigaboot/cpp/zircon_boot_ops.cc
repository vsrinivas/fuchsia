// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon_boot/zircon_boot.h>

namespace gigaboot {
namespace {

bool ReadFromPartition(ZirconBootOps* ops, const char* part, size_t offset, size_t size, void* dst,
                       size_t* read_size) {
  // TODO(b/235489025): To implement. Read data from the GPT partition. Refer to logic in
  // `src/firmware/gigaboot/src/diskio.c`
  return false;
}

bool WriteToPartition(ZirconBootOps* ops, const char* part, size_t offset, size_t size,
                      const void* src, size_t* write_size) {
  // TODO(b/235489025): To implement. Write data to the GPT partition. Refer to logic in
  // `src/firmware/gigaboot/src/diskio.c`
  return false;
}

void Boot(ZirconBootOps* ops, zbi_header_t* image, size_t capacity, AbrSlotIndex slot) {
  // TODO(b/235489025): To implement. The C gigaboot implementation is in function zbi_boot() in
  // `src/firmware/gigaboot/src/zircon.c`. But this can potentially also be done using the boot-shim
  // utils from `zircon/kernel/phys/include/phys/boot-zbi.h`. See
  // `zircon/kernel/phys/boot-shim/zbi-boot-shim.cc` for an example.
}

bool AddZbiItems(ZirconBootOps* ops, zbi_header_t* image, size_t capacity, AbrSlotIndex slot) {
  // TODO(b/235489025): To implement. Append necessary ZBI items for booting the ZBI image. Refers
  // to the C Gigaboot implementation in function boot_zircon() in
  // `src/firmware/gigaboot/src/zircon.c` for what items are needed.
  return false;
}

}  // namespace

ZirconBootOps GetZirconBootOps() {
  ZirconBootOps zircon_boot_ops;
  zircon_boot_ops.context = nullptr;
  zircon_boot_ops.read_from_partition = ReadFromPartition;
  zircon_boot_ops.write_to_partition = WriteToPartition;
  zircon_boot_ops.boot = Boot;
  zircon_boot_ops.add_zbi_items = AddZbiItems;
  zircon_boot_ops.get_firmware_slot = nullptr;

  // TODO(b/235489025): Implement the following callbacks for libavb integration. These operations
  // might differ from product to product. Thus we may need to implement them as a configurable
  // sysdeps and let product specify them.
  zircon_boot_ops.verified_boot_get_partition_size = nullptr;
  zircon_boot_ops.verified_boot_read_rollback_index = nullptr;
  zircon_boot_ops.verified_boot_write_rollback_index = nullptr;
  zircon_boot_ops.verified_boot_read_is_device_locked = nullptr;
  zircon_boot_ops.verified_boot_read_permanent_attributes = nullptr;
  zircon_boot_ops.verified_boot_read_permanent_attributes_hash = nullptr;
  return zircon_boot_ops;
}

}  // namespace gigaboot
