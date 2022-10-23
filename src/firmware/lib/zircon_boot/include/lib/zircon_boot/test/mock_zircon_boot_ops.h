/* Copyright 2020 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SRC_FIRMWARE_LIB_ZIRCON_BOOT_INCLUDE_LIB_ZIRCON_BOOT_TEST_MOCK_ZIRCON_BOOT_OPS_H_
#define SRC_FIRMWARE_LIB_ZIRCON_BOOT_INCLUDE_LIB_ZIRCON_BOOT_TEST_MOCK_ZIRCON_BOOT_OPS_H_

#include <lib/stdcompat/span.h>
#include <lib/zbi/zbi.h>
#include <lib/zircon_boot/zircon_boot.h>
#include <lib/zx/result.h>

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class MockZirconBootOps {
 public:
  enum class LockStatus {
    kLocked,
    kUnlocked,
  };

  MockZirconBootOps() = default;

  // Basic ops
  zx::result<> ReadFromPartition(const char* part, size_t offset, size_t size, void* out);
  zx::result<> WriteToPartition(const char* part, size_t offset, size_t size, const void* payload);
  zx::result<size_t> GetPartitionSize(const char* part);
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

  // Verified boot related.
  void WriteRollbackIndex(size_t location, uint64_t rollback_index);
  zx::result<uint64_t> ReadRollbackIndex(size_t location) const;
  LockStatus GetDeviceLockStatus() { return device_locked_status_; }
  void SetDeviceLockStatus(LockStatus status) { device_locked_status_ = status; }
  AvbAtxPermanentAttributes GetPermanentAttributes();
  void SetPermanentAttributes(const AvbAtxPermanentAttributes& permanent_attribute);
  ZirconBootOps GetZirconBootOps();
  ZirconBootOps GetZirconBootOpsWithAvb();

 private:
  std::unordered_map<std::string, std::vector<uint8_t>> partitions_;
  std::unordered_map<size_t, uint64_t> rollback_index_;
  std::unordered_map<std::string, std::vector<uint8_t>> persistent_value_;
  LockStatus device_locked_status_ = LockStatus::kLocked;
  AbrSlotIndex firmware_slot_;
  std::vector<uint8_t> booted_image_;
  std::optional<AbrSlotIndex> booted_slot_;
  std::function<bool(zbi_header_t*, size_t, AbrSlotIndex)> add_zbi_items_;
  AvbAtxPermanentAttributes permanent_attributes_;

  zx::result<cpp20::span<uint8_t>> GetPartitionSpan(const char* name, size_t offset, size_t size);

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

  // For assigning to ZirconVBootOps
  static bool GetPartitionSize(ZirconBootOps* ops, const char* part, size_t* out);
  static bool ReadRollbackIndex(ZirconBootOps* ops, size_t rollback_index_location,
                                uint64_t* out_rollback_index);
  static bool WriteRollbackIndex(ZirconBootOps* ops, size_t rollback_index_location,
                                 uint64_t rollback_index);
  static bool ReadIsDeivceLocked(ZirconBootOps* ops, bool* out_is_locked);
  static bool ReadPermanentAttributes(ZirconBootOps* ops, AvbAtxPermanentAttributes* attribute);
  static bool ReadPermanentAttributesHash(ZirconBootOps* ops, uint8_t hash[AVB_SHA256_DIGEST_SIZE]);
};

#endif  // SRC_FIRMWARE_LIB_ZIRCON_BOOT_INCLUDE_LIB_ZIRCON_BOOT_TEST_MOCK_ZIRCON_BOOT_OPS_H_
