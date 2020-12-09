/* Copyright 2020 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock_zircon_boot_ops.h"

#include <fcntl.h>
#include <zircon/hw/gpt.h>

zx::status<fbl::Span<uint8_t>> MockZirconBootOps::GetPartitionSpan(const char* part_name,
                                                                   size_t offset, size_t size) {
  auto part = partitions_.find(part_name);
  if (part == partitions_.end()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  std::vector<uint8_t>& data = part->second;
  if (offset > data.size() || data.size() - offset < size) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  return zx::ok(fbl::Span<uint8_t>(data.data() + offset, size));
}

zx::status<> MockZirconBootOps::ReadFromPartition(const char* part_name, size_t offset, size_t size,
                                                  void* out) {
  auto status = GetPartitionSpan(part_name, offset, size);
  if (status.is_error()) {
    return status.take_error();
  }
  memcpy(out, status.value().data(), status.value().size_bytes());
  return zx::ok();
}

zx::status<> MockZirconBootOps::WriteToPartition(const char* part_name, size_t offset, size_t size,
                                                 const void* payload) {
  auto status = GetPartitionSpan(part_name, offset, size);
  if (status.is_error()) {
    return status.take_error();
  }
  auto start = static_cast<const uint8_t*>(payload);
  std::copy(start, start + size, status.value().data());
  return zx::ok();
}

zx::status<size_t> MockZirconBootOps::GetPartitionSize(const char* part_name) {
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

ZirconBootOps MockZirconBootOps::GetZirconBootOps() {
  ZirconBootOps zircon_boot_ops;
  zircon_boot_ops.context = this;
  zircon_boot_ops.read_from_partition = ReadFromPartition;
  zircon_boot_ops.write_to_partition = WriteToPartition;
  zircon_boot_ops.get_firmware_slot = GetFirmwareSlot;
  zircon_boot_ops.reboot = Reboot;
  zircon_boot_ops.boot = Boot;
  return zircon_boot_ops;
}
