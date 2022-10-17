/* Copyright 2020 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <fcntl.h>
#include <lib/cksum.h>
#include <lib/zircon_boot/test/mock_zircon_boot_ops.h>
#include <zircon/hw/gpt.h>

#include "src/lib/digest/digest.h"

uint32_t AbrCrc32(const void* buf, size_t buf_size) {
  return crc32(0, reinterpret_cast<const uint8_t*>(buf), buf_size);
}

zx::result<cpp20::span<uint8_t>> MockZirconBootOps::GetPartitionSpan(const char* part_name,
                                                                     size_t offset, size_t size) {
  auto part = partitions_.find(part_name);
  if (part == partitions_.end()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  std::vector<uint8_t>& data = part->second;
  if (offset > data.size() || data.size() - offset < size) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  return zx::ok(cpp20::span<uint8_t>(data.data() + offset, size));
}

zx::result<> MockZirconBootOps::ReadFromPartition(const char* part_name, size_t offset, size_t size,
                                                  void* out) {
  auto status = GetPartitionSpan(part_name, offset, size);
  if (status.is_error()) {
    return status.take_error();
  }
  memcpy(out, status.value().data(), status.value().size_bytes());
  return zx::ok();
}

zx::result<> MockZirconBootOps::WriteToPartition(const char* part_name, size_t offset, size_t size,
                                                 const void* payload) {
  auto status = GetPartitionSpan(part_name, offset, size);
  if (status.is_error()) {
    return status.take_error();
  }
  auto start = static_cast<const uint8_t*>(payload);
  std::copy(start, start + size, status.value().data());
  return zx::ok();
}

zx::result<size_t> MockZirconBootOps::GetPartitionSize(const char* part_name) {
  auto part = partitions_.find(part_name);
  if (part == partitions_.end()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  return zx::ok(part->second.size());
}

void MockZirconBootOps::AddPartition(const char* name, size_t size) {
  partitions_[name] = std::vector<uint8_t>(size, 0);
}

void MockZirconBootOps::Boot(zbi_header_t* image, size_t capacity, AbrSlotIndex slot) {
  const uint8_t* start = reinterpret_cast<const uint8_t*>(image);
  booted_image_ = std::vector<uint8_t>(start, start + capacity);
  booted_slot_ = slot;
}

void MockZirconBootOps::Reboot(bool force_recovery) {
  // For firmware A/B/R, we assume that device boots to slot according to metadata.
  AbrOps ops = GetAbrOps();
  firmware_slot_ =
      force_recovery == kForceRecoveryOn ? kAbrSlotIndexR : AbrGetBootSlot(&ops, false, nullptr);
}

static bool ReadAbrMetadata(void* context, size_t size, uint8_t* buffer) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(context);
  return dev->ReadFromPartition(GPT_DURABLE_BOOT_NAME, 0, size, buffer).is_ok();
}

static bool WriteAbrMetadata(void* context, const uint8_t* buffer, size_t size) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(context);
  return dev->WriteToPartition(GPT_DURABLE_BOOT_NAME, 0, size, buffer).is_ok();
}

AbrOps MockZirconBootOps::GetAbrOps() {
  return AbrOps{
      .context = this,
      .read_abr_metadata = ReadAbrMetadata,
      .write_abr_metadata = WriteAbrMetadata,
  };
}

void MockZirconBootOps::SetAddDeviceZbiItemsMethod(
    std::function<bool(zbi_header_t*, size_t, AbrSlotIndex)> method) {
  add_zbi_items_ = method;
}

void MockZirconBootOps::WriteRollbackIndex(size_t location, uint64_t rollback_index) {
  rollback_index_[location] = rollback_index;
}

zx::result<uint64_t> MockZirconBootOps::ReadRollbackIndex(size_t location) const {
  auto iter = rollback_index_.find(location);
  if (iter == rollback_index_.end()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  return zx::ok(iter->second);
}

void MockZirconBootOps::SetPermanentAttributes(
    const AvbAtxPermanentAttributes& permanent_attribute) {
  permanent_attributes_ = permanent_attribute;
}

AvbAtxPermanentAttributes MockZirconBootOps::GetPermanentAttributes() {
  return permanent_attributes_;
}

bool MockZirconBootOps::GetPartitionSize(ZirconBootOps* ops, const char* part, size_t* out) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(ops->context);
  auto status = dev->GetPartitionSize(part);
  if (status.is_error()) {
    return 0;
  }
  *out = status.value();
  return true;
}

bool MockZirconBootOps::ReadFromPartition(ZirconBootOps* ops, const char* part, size_t offset,
                                          size_t size, void* dst, size_t* read_size) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(ops->context);
  if (auto status = dev->ReadFromPartition(part, offset, size, static_cast<uint8_t*>(dst));
      status.is_error()) {
    return false;
  }
  *read_size = size;
  return true;
}

bool MockZirconBootOps::WriteToPartition(ZirconBootOps* ops, const char* part, size_t offset,
                                         size_t size, const void* src, size_t* write_size) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(ops->context);
  if (auto status = dev->WriteToPartition(part, offset, size, static_cast<const uint8_t*>(src));
      status.is_error()) {
    return false;
  }
  *write_size = size;
  return true;
}

bool MockZirconBootOps::GetFirmwareSlot(ZirconBootOps* ops, AbrSlotIndex* out_slot) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(ops->context);
  *out_slot = dev->GetFirmwareSlot();
  return true;
}

void MockZirconBootOps::Reboot(ZirconBootOps* ops, bool force_recovery) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(ops->context);
  dev->Reboot(force_recovery);
}

void MockZirconBootOps::Boot(ZirconBootOps* ops, zbi_header_t* image, size_t capacity,
                             AbrSlotIndex slot) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(ops->context);
  dev->Boot(image, capacity, slot);
}

bool MockZirconBootOps::AddDeviceZbiItems(ZirconBootOps* zb_ops, zbi_header_t* image,
                                          size_t capacity, AbrSlotIndex slot) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(zb_ops->context);
  return dev->add_zbi_items_(image, capacity, slot);
}

// Avb related operation
bool MockZirconBootOps::ReadRollbackIndex(ZirconBootOps* ops, size_t rollback_index_location,
                                          uint64_t* out_rollback_index) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(ops->context);
  auto status = dev->ReadRollbackIndex(rollback_index_location);
  if (status.is_error()) {
    return false;
  }
  *out_rollback_index = status.value();
  return true;
}

bool MockZirconBootOps::WriteRollbackIndex(ZirconBootOps* ops, size_t rollback_index_location,
                                           uint64_t rollback_index) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(ops->context);
  dev->WriteRollbackIndex(rollback_index_location, rollback_index);
  return true;
}

bool MockZirconBootOps::ReadIsDeivceLocked(ZirconBootOps* ops, bool* out_is_locked) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(ops->context);
  *out_is_locked = dev->GetDeviceLockStatus() == MockZirconBootOps::LockStatus::kLocked;
  return true;
}

bool MockZirconBootOps::ReadPermanentAttributes(ZirconBootOps* ops,
                                                AvbAtxPermanentAttributes* attribute) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(ops->context);
  *attribute = dev->GetPermanentAttributes();
  return true;
}

bool MockZirconBootOps::ReadPermanentAttributesHash(ZirconBootOps* ops,
                                                    uint8_t hash[AVB_SHA256_DIGEST_SIZE]) {
  MockZirconBootOps* dev = static_cast<MockZirconBootOps*>(ops->context);
  auto attributes = dev->GetPermanentAttributes();
  digest::Digest hasher;
  const uint8_t* result = hasher.Hash(static_cast<void*>(&attributes), sizeof(attributes));
  memcpy(hash, result, AVB_SHA256_DIGEST_SIZE);
  return true;
}

ZirconBootOps MockZirconBootOps::GetZirconBootOps() {
  ZirconBootOps zircon_boot_ops;
  zircon_boot_ops.context = this;
  zircon_boot_ops.read_from_partition = ReadFromPartition;
  zircon_boot_ops.write_to_partition = WriteToPartition;
  zircon_boot_ops.get_firmware_slot = GetFirmwareSlot;
  zircon_boot_ops.reboot = Reboot;
  zircon_boot_ops.boot = Boot;
  zircon_boot_ops.add_zbi_items = AddDeviceZbiItems;
  zircon_boot_ops.verified_boot_get_partition_size = nullptr;
  zircon_boot_ops.verified_boot_read_rollback_index = nullptr;
  zircon_boot_ops.verified_boot_write_rollback_index = nullptr;
  zircon_boot_ops.verified_boot_read_is_device_locked = nullptr;
  zircon_boot_ops.verified_boot_read_permanent_attributes = nullptr;
  zircon_boot_ops.verified_boot_read_permanent_attributes_hash = nullptr;
  return zircon_boot_ops;
}

ZirconBootOps MockZirconBootOps::GetZirconBootOpsWithAvb() {
  ZirconBootOps zircon_boot_ops = GetZirconBootOps();
  zircon_boot_ops.verified_boot_get_partition_size = GetPartitionSize;
  zircon_boot_ops.verified_boot_read_rollback_index = ReadRollbackIndex;
  zircon_boot_ops.verified_boot_write_rollback_index = WriteRollbackIndex;
  zircon_boot_ops.verified_boot_read_is_device_locked = ReadIsDeivceLocked;
  zircon_boot_ops.verified_boot_read_permanent_attributes = ReadPermanentAttributes;
  zircon_boot_ops.verified_boot_read_permanent_attributes_hash = ReadPermanentAttributesHash;
  return zircon_boot_ops;
}
