// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/error-stdio.h>
#include <lib/zircon_boot/zircon_boot.h>

#include <phys/boot-zbi.h>

#include "backends.h"
#include "boot_zbi_items.h"
#include "gpt.h"

namespace gigaboot {
namespace {

// TODO(b/235489025): NUC GPT partitions still uses legacy zircon partition names. Thus we use the
// following mapping as a workaround so that zircon_boot library works correctly. Remove this after
// we update the GPT partition table to use new partition names.
const char* MapPartitionName(const char* name) {
  static const struct PartitionMap {
    const char* part_name;
    const char* mapped_name;
  } kPartitionNameMap[] = {
      {GPT_DURABLE_BOOT_NAME, "misc"},
      {GPT_ZIRCON_A_NAME, "zircon-a"},
      {GPT_ZIRCON_B_NAME, "zircon-b"},
      {GPT_ZIRCON_R_NAME, "zircon-r"},
  };

  for (auto ele : kPartitionNameMap) {
    if (strcmp(ele.part_name, name) == 0) {
      return ele.mapped_name;
    }
  }

  return name;
}

bool ReadFromPartition(ZirconBootOps* ops, const char* part, size_t offset, size_t size, void* dst,
                       size_t* read_size) {
  auto gpt_device = FindEfiGptDevice();
  if (gpt_device.is_error()) {
    return false;
  }

  auto load_res = gpt_device.value().Load();
  if (load_res.is_error()) {
    return false;
  }

  *read_size = size;
  return gpt_device.value().ReadPartition(MapPartitionName(part), offset, size, dst).is_ok();
}

bool WriteToPartition(ZirconBootOps* ops, const char* part, size_t offset, size_t size,
                      const void* src, size_t* write_size) {
  auto gpt_device = FindEfiGptDevice();
  if (gpt_device.is_error()) {
    return false;
  }

  auto load_res = gpt_device.value().Load();
  if (load_res.is_error()) {
    return false;
  }

  *write_size = size;
  return gpt_device.value().WritePartition(MapPartitionName(part), src, offset, size).is_ok();
}

void Boot(ZirconBootOps* ops, zbi_header_t* zbi, size_t capacity, AbrSlotIndex slot) {
  // TODO(https://fxbug.dev/78965): Implement the same relocation logic in zircon_boot
  // library and use it here to validate.
  printf("Booting zircon\n");

  BootZbi::InputZbi input_zbi_view(
      zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi)));

  BootZbi boot;
  if (auto result = boot.Init(input_zbi_view); result.is_error()) {
    printf("boot: Not a bootable ZBI: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  if (auto result = boot.Load(); result.is_error()) {
    printf("boot: Failed to load ZBI: ");
    zbitl::PrintViewCopyError(result.error_value());
    abort();
  }

  // TODO(b/235489025): Perform ExitBootService() here.

  boot.Boot();
}

bool AddZbiItems(ZirconBootOps* ops, zbi_header_t* image, size_t capacity, AbrSlotIndex slot) {
  // TODO(b/235489025): To implement. Append necessary ZBI items for booting the ZBI image. Refers
  // to the C Gigaboot implementation in function boot_zircon() in
  // `src/firmware/gigaboot/src/zircon.c` for what items are needed.
  return AddGigabootZbiItems(image, capacity, slot);
}

bool ReadPermanentAttributes(ZirconBootOps* ops, AvbAtxPermanentAttributes* attribute) {
  ZX_ASSERT(attribute);
  const cpp20::span<const uint8_t> perm_attr = GetPermanentAttributes();
  if (perm_attr.size() != sizeof(AvbAtxPermanentAttributes)) {
    return false;
  }

  memcpy(attribute, perm_attr.data(), perm_attr.size());
  return true;
}

bool ReadPermanentAttributesHash(ZirconBootOps* ops, uint8_t* hash) {
  ZX_ASSERT(hash);
  const cpp20::span<const uint8_t> perm_attr_hash = GetPermanentAttributesHash();
  memcpy(hash, perm_attr_hash.data(), perm_attr_hash.size());
  return true;
}

}  // namespace

ZirconBootOps GetZirconBootOps() {
  ZirconBootOps zircon_boot_ops;
  zircon_boot_ops.context = nullptr;
  zircon_boot_ops.read_from_partition = ReadFromPartition;
  zircon_boot_ops.write_to_partition = WriteToPartition;
  zircon_boot_ops.boot = Boot;
  zircon_boot_ops.add_zbi_items = AddZbiItems;
  zircon_boot_ops.firmware_can_boot_kernel_slot = nullptr;

  // TODO(b/235489025): Implement the following callbacks for libavb integration. These operations
  // might differ from product to product. Thus we may need to implement them as a configurable
  // sysdeps and let product specify them.
  zircon_boot_ops.verified_boot_get_partition_size = nullptr;
  zircon_boot_ops.verified_boot_read_rollback_index = nullptr;
  zircon_boot_ops.verified_boot_write_rollback_index = nullptr;
  zircon_boot_ops.verified_boot_read_is_device_locked = nullptr;
  zircon_boot_ops.verified_boot_read_permanent_attributes = ReadPermanentAttributes;
  zircon_boot_ops.verified_boot_read_permanent_attributes_hash = ReadPermanentAttributesHash;
  return zircon_boot_ops;
}

}  // namespace gigaboot
