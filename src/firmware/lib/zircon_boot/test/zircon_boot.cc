// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/abr/data.h>
#include <lib/cksum.h>
#include <lib/zbi/zbi.h>
#include <lib/zircon_boot/zircon_boot.h>
#include <zircon/hw/gpt.h>

#include <set>
#include <vector>

#include <zxtest/zxtest.h>

#include "mock_zircon_boot_ops.h"

uint32_t AbrCrc32(const void* buf, size_t buf_size) {
  return crc32(0, reinterpret_cast<const uint8_t*>(buf), buf_size);
}

namespace {

constexpr size_t kZirconPartitionSize = 128 * 1024;
constexpr size_t kVbmetaPartitionSize = 64 * 1024;
constexpr size_t kImageSize = 1024;

void CreateMockZirconBootOps(std::unique_ptr<MockZirconBootOps>* out) {
  auto device = std::make_unique<MockZirconBootOps>();

  // durable boot
  device->AddPartition(GPT_DURABLE_BOOT_NAME, sizeof(AbrData));

  // TODO(b/174968242): For now we just initialize images to zeros. Once verified
  // boot logic is integrated into this library. The initial data for each slot will be
  // replaced with different test zircon images for testing slot verification logic.
  struct Image {
    zbi_header_t hdr;
    zbi_header_t kernel;
    uint8_t payload[ZBI_ALIGN(kImageSize)];
  } image;
  std::vector<uint8_t> zero_zircon_image(kImageSize, 0);
  ASSERT_EQ(zbi_init(&image, sizeof(image)), ZBI_RESULT_OK);
  auto res = zbi_create_entry_with_payload(&image, sizeof(image), ZBI_TYPE_KERNEL_ARM64, 0, 0,
                                           zero_zircon_image.data(),
                                           static_cast<uint32_t>(zero_zircon_image.size()));
  ASSERT_EQ(res, ZBI_RESULT_OK);
  // zircon partitions
  struct PartitionAndData {
    const char* name;
    const void* data;
    size_t data_len;
  } zircon_partitions[] = {
      {GPT_ZIRCON_A_NAME, &image, sizeof(image)},
      {GPT_ZIRCON_B_NAME, &image, sizeof(image)},
      {GPT_ZIRCON_R_NAME, &image, sizeof(image)},
  };
  for (auto& ele : zircon_partitions) {
    device->AddPartition(ele.name, kZirconPartitionSize);
    ASSERT_OK(device->WriteToPartition(ele.name, 0, ele.data_len, ele.data));
  }

  // TODO(b/174968242): Once verified boot logic is integrated into this library, the initial data
  // will be replaced with actual test vbmeta images for the corresponding slot images..
  std::vector<uint8_t> zero_vbmeta_image(kVbmetaPartitionSize, 0);
  // vbmeta partitions
  PartitionAndData vbmeta_partitions[] = {
      {GPT_VBMETA_A_NAME, zero_vbmeta_image.data(), zero_vbmeta_image.size()},
      {GPT_VBMETA_B_NAME, zero_vbmeta_image.data(), zero_vbmeta_image.size()},
      {GPT_VBMETA_R_NAME, zero_vbmeta_image.data(), zero_vbmeta_image.size()},
  };
  for (auto& ele : vbmeta_partitions) {
    device->AddPartition(ele.name, kVbmetaPartitionSize);
    ASSERT_OK(device->WriteToPartition(ele.name, 0, ele.data_len, ele.data));
  }

  *out = std::move(device);
}

void MarkSlotActive(MockZirconBootOps* dev, AbrSlotIndex slot) {
  AbrOps abr_ops = dev->GetAbrOps();
  if (slot != kAbrSlotIndexR) {
    AbrMarkSlotActive(&abr_ops, slot);
  } else {
    AbrMarkSlotUnbootable(&abr_ops, kAbrSlotIndexA);
    AbrMarkSlotUnbootable(&abr_ops, kAbrSlotIndexB);
  }
}

void ValidateBootedSlot(const MockZirconBootOps* dev, AbrSlotIndex expected_slot) {
  auto booted_slot = dev->GetBootedSlot();
  ASSERT_TRUE(booted_slot.has_value());
  ASSERT_EQ(*booted_slot, expected_slot);
  // TODO(b/174968242): Once zbi handling logic is integrated, validate that the expected zbi
  // items are added.
}

// Tests that boot logic for OS ABR works correctly.
// ABR metadata is initialized to mark |initial_active_slot| as the active slot.
// |expected_slot| specifies the resulting booted slot.
void TestOsAbrSuccessfulBoot(AbrSlotIndex initial_active_slot, AbrSlotIndex expected_slot,
                             ForceRecovery force_recovery) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURES(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  zircon_boot_ops.get_firmware_slot = nullptr;
  MarkSlotActive(dev.get(), initial_active_slot);
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&zircon_boot_ops, buffer.data(), buffer.size(), force_recovery),
            kBootResultBootReturn);
  ASSERT_NO_FATAL_FAILURES(ValidateBootedSlot(dev.get(), expected_slot));
}

TEST(BootTests, TestSuccessfulBootOsAbr) {
  ASSERT_NO_FATAL_FAILURES(
      TestOsAbrSuccessfulBoot(kAbrSlotIndexA, kAbrSlotIndexA, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURES(
      TestOsAbrSuccessfulBoot(kAbrSlotIndexB, kAbrSlotIndexB, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURES(
      TestOsAbrSuccessfulBoot(kAbrSlotIndexR, kAbrSlotIndexR, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURES(
      TestOsAbrSuccessfulBoot(kAbrSlotIndexA, kAbrSlotIndexR, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURES(
      TestOsAbrSuccessfulBoot(kAbrSlotIndexB, kAbrSlotIndexR, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURES(
      TestOsAbrSuccessfulBoot(kAbrSlotIndexR, kAbrSlotIndexR, kForceRecoveryOn));
}

// Tests that OS ABR booting logic detects zbi header corruption and falls back to the other slots.
// |corrupt_hdr| is a function that specifies how header should be corrupted.
void TestInvalidZbiHeaderOsAbr(std::function<void(zbi_header_t* hdr)> corrupt_hdr) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURES(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  zircon_boot_ops.get_firmware_slot = nullptr;
  // corrupt header
  zbi_header_t header;
  ASSERT_OK(dev->ReadFromPartition(GPT_ZIRCON_A_NAME, 0, sizeof(header), &header));
  corrupt_hdr(&header);
  ASSERT_OK(dev->WriteToPartition(GPT_ZIRCON_A_NAME, 0, sizeof(header), &header));

  // Boot to the corrupted slot A first.
  AbrOps abr_ops = dev->GetAbrOps();
  AbrMarkSlotActive(&abr_ops, kAbrSlotIndexA);

  std::vector<uint8_t> buffer(kZirconPartitionSize);
  // Slot A should fail and fall back to slot B
  ASSERT_EQ(LoadAndBoot(&zircon_boot_ops, buffer.data(), buffer.size(), kForceRecoveryOff),
            kBootResultBootReturn);
  ASSERT_NO_FATAL_FAILURES(ValidateBootedSlot(dev.get(), kAbrSlotIndexB));
  // Slot A unbootable
  AbrData abr_data;
  ASSERT_OK(dev->ReadFromPartition(GPT_DURABLE_BOOT_NAME, 0, sizeof(abr_data), &abr_data));
  ASSERT_EQ(abr_data.slot_data[0].tries_remaining, 0);
  ASSERT_EQ(abr_data.slot_data[0].successful_boot, 0);
  ASSERT_EQ(abr_data.slot_data[0].priority, 0);
}

TEST(BootTests, LoadAndBootInvalidZbiHeaderType) {
  ASSERT_NO_FATAL_FAILURES(TestInvalidZbiHeaderOsAbr([](auto hdr) { hdr->type = 0; }));
}

TEST(BootTests, LoadAndBootInvalidZbiHeaderExtra) {
  ASSERT_NO_FATAL_FAILURES(TestInvalidZbiHeaderOsAbr([](auto hdr) { hdr->extra = 0; }));
}

TEST(BootTests, LoadAndBootInvalidZbiHeaderMagic) {
  ASSERT_NO_FATAL_FAILURES(TestInvalidZbiHeaderOsAbr([](auto hdr) { hdr->magic = 0; }));
}

TEST(BootTests, LoadAndBootImageTooLarge) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURES(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  zircon_boot_ops.get_firmware_slot = nullptr;
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&zircon_boot_ops, buffer.data(), kImageSize - 1, kForceRecoveryOff),
            kBootResultErrorNoValidSlot);
}

// Tests that firmware ABR logic correctly boots to the matching firmware slot.
// |current_firmware_slot| represents the slot fo currently running firmware. |initial_active_slot|
// represents the active slot by metadata.
void TestFirmareAbrMatchingSlotBootSucessful(AbrSlotIndex current_firmware_slot,
                                             AbrSlotIndex initial_active_slot,
                                             ForceRecovery force_recovery) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURES(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  dev->SetFirmwareSlot(current_firmware_slot);
  MarkSlotActive(dev.get(), initial_active_slot);
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&zircon_boot_ops, buffer.data(), buffer.size(), force_recovery),
            kBootResultBootReturn);
  ASSERT_NO_FATAL_FAILURES(ValidateBootedSlot(dev.get(), current_firmware_slot));
}

TEST(BootTests, LoadAndBootMatchingSlotBootSucessful) {
  ASSERT_NO_FATAL_FAILURES(
      TestFirmareAbrMatchingSlotBootSucessful(kAbrSlotIndexA, kAbrSlotIndexA, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURES(
      TestFirmareAbrMatchingSlotBootSucessful(kAbrSlotIndexB, kAbrSlotIndexB, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURES(
      TestFirmareAbrMatchingSlotBootSucessful(kAbrSlotIndexR, kAbrSlotIndexR, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURES(
      TestFirmareAbrMatchingSlotBootSucessful(kAbrSlotIndexR, kAbrSlotIndexA, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURES(
      TestFirmareAbrMatchingSlotBootSucessful(kAbrSlotIndexR, kAbrSlotIndexB, kForceRecoveryOn));
}

// Tests that device reboot if firmware slot doesn't matches the target slot to boot. i.e. either
// ABR metadata doesn't match firmware slot or force_recovery is on but device is not in firmware R
// slot.
void TestFirmwareAbrRebootIfSlotMismatched(AbrSlotIndex current_firmware_slot,
                                           AbrSlotIndex initial_active_slot,
                                           AbrSlotIndex expected_firmware_slot,
                                           ForceRecovery force_recovery) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURES(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  dev->SetFirmwareSlot(current_firmware_slot);
  MarkSlotActive(dev.get(), initial_active_slot);
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&zircon_boot_ops, buffer.data(), buffer.size(), force_recovery),
            kBootResultRebootReturn);
  ASSERT_FALSE(dev->GetBootedSlot());
  ASSERT_EQ(dev->GetFirmwareSlot(), expected_firmware_slot);
}

TEST(BootTests, LoadAndBootMismatchedSlotTriggerReboot) {
  ASSERT_NO_FATAL_FAILURES(TestFirmwareAbrRebootIfSlotMismatched(kAbrSlotIndexA, kAbrSlotIndexA,
                                                                 kAbrSlotIndexR, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURES(TestFirmwareAbrRebootIfSlotMismatched(
      kAbrSlotIndexA, kAbrSlotIndexB, kAbrSlotIndexB, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURES(TestFirmwareAbrRebootIfSlotMismatched(
      kAbrSlotIndexA, kAbrSlotIndexR, kAbrSlotIndexR, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURES(TestFirmwareAbrRebootIfSlotMismatched(kAbrSlotIndexB, kAbrSlotIndexB,
                                                                 kAbrSlotIndexR, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURES(TestFirmwareAbrRebootIfSlotMismatched(
      kAbrSlotIndexB, kAbrSlotIndexA, kAbrSlotIndexA, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURES(TestFirmwareAbrRebootIfSlotMismatched(
      kAbrSlotIndexB, kAbrSlotIndexR, kAbrSlotIndexR, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURES(TestFirmwareAbrRebootIfSlotMismatched(
      kAbrSlotIndexR, kAbrSlotIndexA, kAbrSlotIndexA, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURES(TestFirmwareAbrRebootIfSlotMismatched(
      kAbrSlotIndexR, kAbrSlotIndexB, kAbrSlotIndexB, kForceRecoveryOff));
}
}  // namespace
