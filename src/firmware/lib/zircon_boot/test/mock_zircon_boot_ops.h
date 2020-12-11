/* Copyright 2020 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SRC_FIRMWARE_LIB_ZIRCON_BOOT_TEST_MOCK_ZIRCON_BOOT_OPS_H_
#define SRC_FIRMWARE_LIB_ZIRCON_BOOT_TEST_MOCK_ZIRCON_BOOT_OPS_H_

#include <lib/zbi/zbi.h>
#include <lib/zircon_boot/zircon_boot.h>
#include <lib/zx/status.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <fbl/span.h>

class MockZirconBootOps {
 public:
  MockZirconBootOps() = default;

  // Basic ops
  zx::status<> ReadFromPartition(const char* part, size_t offset, size_t size, void* out);
  zx::status<> WriteToPartition(const char* part, size_t offset, size_t size, const void* payload);
  zx::status<size_t> GetPartitionSize(const char* part);
  void AddPartition(const char* name, size_t size);
  void Boot(zbi_header_t* image, size_t capacity, AbrSlotIndex slot);
  std::optional<AbrSlotIndex> GetBootedSlot() const { return booted_slot_; }
  const std::vector<uint8_t>& GetBootedImage() const { return booted_image_; }
  AbrOps GetAbrOps();
  void SetAddDeviceZbiItemsMethod(std::function<bool(zbi_header_t*, size_t, AbrSlotIndex)> method);
  // Firmware ABR related
  AbrSlotIndex GetFirmwareSlot() { return firmware_slot_; }
  void SetFirmwareSlot(AbrSlotIndex slot) { firmware_slot_ = slot; }
  void Reboot(bool force_recovery);

  ZirconBootOps GetZirconBootOps();

 private:
  std::unordered_map<std::string, std::vector<uint8_t>> partitions_;
  AbrSlotIndex firmware_slot_;
  std::vector<uint8_t> booted_image_;
  std::optional<AbrSlotIndex> booted_slot_;
  std::function<bool(zbi_header_t*, size_t, AbrSlotIndex)> add_zbi_items_;

  zx::status<fbl::Span<uint8_t>> GetPartitionSpan(const char* name, size_t offset, size_t size);

  // For assigning to ZirconBootOps
  static bool ReadFromPartition(ZirconBootOps* ops, const char* part, size_t offset, size_t size,
                                void* dst, size_t* read_size);
  static bool WriteToPartition(ZirconBootOps* ops, const char* part, size_t offset, size_t size,
                               const void* src, size_t* write_size);
  static bool GetFirmwareSlot(ZirconBootOps* ops, AbrSlotIndex* out_slot);
  static void Reboot(ZirconBootOps* ops, bool force_recovery);
  static void Boot(ZirconBootOps* ops, zbi_header_t* image, size_t capacity, AbrSlotIndex slot);
  static bool AddDeviceZbiItems(ZirconBootOps* zb_ops, zbi_header_t* image, size_t capacity,
                                AbrSlotIndex slot);
};

#endif  // SRC_FIRMWARE_LIB_ZIRCON_BOOT_TEST_MOCK_ZIRCON_BOOT_OPS_H_
