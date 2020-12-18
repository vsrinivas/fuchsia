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
#include "test_data/test_images.h"

uint32_t AbrCrc32(const void* buf, size_t buf_size) {
  return crc32(0, reinterpret_cast<const uint8_t*>(buf), buf_size);
}

namespace {

constexpr size_t kZirconPartitionSize = 128 * 1024;
constexpr size_t kVbmetaPartitionSize = 64 * 1024;

const char kTestCmdline[] = "foo=bar";

void CreateMockZirconBootOps(std::unique_ptr<MockZirconBootOps>* out) {
  auto device = std::make_unique<MockZirconBootOps>();

  // durable boot
  device->AddPartition(GPT_DURABLE_BOOT_NAME, sizeof(AbrData));

  // zircon partitions
  struct PartitionAndData {
    const char* name;
    const void* data;
    size_t data_len;
  } zircon_partitions[] = {
      {GPT_ZIRCON_A_NAME, kTestZirconAImage, sizeof(kTestZirconAImage)},
      {GPT_ZIRCON_B_NAME, kTestZirconBImage, sizeof(kTestZirconBImage)},
      {GPT_ZIRCON_R_NAME, kTestZirconRImage, sizeof(kTestZirconRImage)},
  };
  for (auto& ele : zircon_partitions) {
    device->AddPartition(ele.name, kZirconPartitionSize);
    ASSERT_OK(device->WriteToPartition(ele.name, 0, ele.data_len, ele.data));
  }

  // vbmeta partitions
  PartitionAndData vbmeta_partitions[] = {
      {GPT_VBMETA_A_NAME, kTestVbmetaAImage, sizeof(kTestVbmetaAImage)},
      {GPT_VBMETA_B_NAME, kTestVbmetaBImage, sizeof(kTestVbmetaBImage)},
      {GPT_VBMETA_R_NAME, kTestVbmetaRImage, sizeof(kTestVbmetaRImage)},
  };
  for (auto& ele : vbmeta_partitions) {
    device->AddPartition(ele.name, kVbmetaPartitionSize);
    ASSERT_OK(device->WriteToPartition(ele.name, 0, ele.data_len, ele.data));
  }

  device->SetAddDeviceZbiItemsMethod([](zbi_header_t* image, size_t capacity, AbrSlotIndex slot) {
    if (AppendCurrentSlotZbiItem(image, capacity, slot) != ZBI_RESULT_OK) {
      return false;
    }

    if (zbi_create_entry_with_payload(image, capacity, ZBI_TYPE_CMDLINE, 0, 0, kTestCmdline,
                                      sizeof(kTestCmdline)) != ZBI_RESULT_OK) {
      return false;
    }
    return true;
  });

  AvbAtxPermanentAttributes permanent_attributes;
  memcpy(&permanent_attributes, kPermanentAttributes, sizeof(permanent_attributes));
  device->SetPermanentAttributes(permanent_attributes);

  for (size_t i = 0; i < AVB_MAX_NUMBER_OF_ROLLBACK_INDEX_LOCATIONS; i++) {
    device->WriteRollbackIndex(i, 0);
  }
  device->WriteRollbackIndex(AVB_ATX_PIK_VERSION_LOCATION, 0);
  device->WriteRollbackIndex(AVB_ATX_PSK_VERSION_LOCATION, 0);

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

// We only care about |type|, |extra| and |payload|
// Use std::tuple for the built-in comparison operator.
using NormalizedZbiItem = std::tuple<uint32_t, uint32_t, std::vector<uint8_t>>;
NormalizedZbiItem NormalizeZbiItem(uint32_t type, uint32_t extra, const void* payload,
                                   size_t size) {
  const uint8_t* start = static_cast<const uint8_t*>(payload);
  return {type, extra, std::vector<uint8_t>(start, start + size)};
}

zbi_result_t ExtractAndSortZbiItemsCallback(zbi_header_t* hdr, void* payload, void* cookie) {
  std::multiset<NormalizedZbiItem>* out = static_cast<std::multiset<NormalizedZbiItem>*>(cookie);
  if (hdr->type == ZBI_TYPE_KERNEL_ARM64) {
    return ZBI_RESULT_OK;
  }
  out->insert(NormalizeZbiItem(hdr->type, hdr->extra, payload, hdr->length));
  return ZBI_RESULT_OK;
}

void ExtractAndSortZbiItems(const uint8_t* buffer, std::multiset<NormalizedZbiItem>* out) {
  out->clear();
  ASSERT_EQ(zbi_for_each(buffer, ExtractAndSortZbiItemsCallback, out), ZBI_RESULT_OK);
}

void ValidateBootedSlot(const MockZirconBootOps* dev, AbrSlotIndex expected_slot) {
  auto booted_slot = dev->GetBootedSlot();
  ASSERT_TRUE(booted_slot.has_value());
  ASSERT_EQ(*booted_slot, expected_slot);
  // Use multiset so that we can catch bugs such as duplicated append.
  std::multiset<NormalizedZbiItem> zbi_items_added;
  ASSERT_NO_FAILURES(ExtractAndSortZbiItems(dev->GetBootedImage().data(), &zbi_items_added));

  std::string current_slot = "zvb.current_slot=" + std::string(AbrGetSlotSuffix(expected_slot));
  std::multiset<NormalizedZbiItem> zbi_items_expected = {
      // Verify that the current slot item is appended. (plus 1 for null terminator)
      NormalizeZbiItem(ZBI_TYPE_CMDLINE, 0, current_slot.data(), current_slot.size() + 1),
      // Verify that the additional cmdline item is appended.
      NormalizeZbiItem(ZBI_TYPE_CMDLINE, 0, kTestCmdline, sizeof(kTestCmdline)),
  };
  // Exactly the above items are appended. No more, no less.
  EXPECT_EQ(zbi_items_added, zbi_items_expected);
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

TEST(BootTests, SkipAddZbiItems) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURES(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  zircon_boot_ops.get_firmware_slot = nullptr;
  zircon_boot_ops.add_zbi_items = nullptr;
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&zircon_boot_ops, buffer.data(), buffer.size(), kForceRecoveryOff),
            kBootResultBootReturn);
  std::multiset<NormalizedZbiItem> zbi_items_added;
  ASSERT_NO_FAILURES(ExtractAndSortZbiItems(dev->GetBootedImage().data(), &zbi_items_added));
  ASSERT_TRUE(zbi_items_added.empty());
}

// Tests that OS ABR booting logic detects ZBI header corruption and falls back to the other slots.
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
  ASSERT_EQ(LoadAndBoot(&zircon_boot_ops, buffer.data(), sizeof(kTestZirconAImage) - 1,
                        kForceRecoveryOff),
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

// TODO(b/174968242): Update the test to check booted slot, ZBI item from property descriptors
TEST(BootTests, TestSuccessfulVerifiedBootOsAbr) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURES(CreateMockZirconBootOps(&dev));
  ZirconBootOps ops = dev->GetZirconBootOpsWithAvb();
  ops.get_firmware_slot = nullptr;
  MarkSlotActive(dev.get(), kAbrSlotIndexA);
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&ops, buffer.data(), buffer.size(), kForceRecoveryOff),
            kBootResultBootReturn);
  ASSERT_NO_FATAL_FAILURES(ValidateBootedSlot(dev.get(), kAbrSlotIndexA));
}
}  // namespace
