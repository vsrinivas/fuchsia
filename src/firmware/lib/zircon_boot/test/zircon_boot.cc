// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/abr/data.h>
#include <lib/zbi/zbi.h>
#include <lib/zircon_boot/test/mock_zircon_boot_ops.h>
#include <lib/zircon_boot/zircon_boot.h>
#include <zircon/hw/gpt.h>

#include <set>
#include <vector>

#include <zxtest/zxtest.h>

#include "test_data/test_images.h"

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

TEST(BootTests, InvalidMemoryBufferErrorsOut) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  zircon_boot_ops.firmware_can_boot_kernel_slot = nullptr;
  MarkSlotActive(dev.get(), kAbrSlotIndexA);
  ASSERT_EQ(LoadAndBoot(&zircon_boot_ops, nullptr, kZirconPartitionSize, kForceRecoveryOff),
            kBootResultErrorInvalidArguments);
}

// Tests that boot logic for OS ABR works correctly.
// ABR metadata is initialized to mark |initial_active_slot| as the active slot.
// |expected_slot| specifies the resulting booted slot.
void TestOsAbrSuccessfulBoot(AbrSlotIndex initial_active_slot, AbrSlotIndex expected_slot,
                             ForceRecovery force_recovery) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  zircon_boot_ops.firmware_can_boot_kernel_slot = nullptr;
  MarkSlotActive(dev.get(), initial_active_slot);
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&zircon_boot_ops, buffer.data(), buffer.size(), force_recovery),
            kBootResultBootReturn);
  ASSERT_NO_FATAL_FAILURE(ValidateBootedSlot(dev.get(), expected_slot));
}

void TestOsAbrSuccessfulBootOneShotRecovery(ForceRecovery force_recovery) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  zircon_boot_ops.firmware_can_boot_kernel_slot = nullptr;
  AbrOps abr_ops = dev->GetAbrOps();
  AbrSetOneShotRecovery(&abr_ops, true);
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&zircon_boot_ops, buffer.data(), buffer.size(), force_recovery),
            kBootResultBootReturn);
  ASSERT_NO_FATAL_FAILURE(ValidateBootedSlot(dev.get(), kAbrSlotIndexR));
}

TEST(BootTests, TestSuccessfulBootOsAbr) {
  ASSERT_NO_FATAL_FAILURE(
      TestOsAbrSuccessfulBoot(kAbrSlotIndexA, kAbrSlotIndexA, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(
      TestOsAbrSuccessfulBoot(kAbrSlotIndexB, kAbrSlotIndexB, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(
      TestOsAbrSuccessfulBoot(kAbrSlotIndexR, kAbrSlotIndexR, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(TestOsAbrSuccessfulBootOneShotRecovery(kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(
      TestOsAbrSuccessfulBoot(kAbrSlotIndexA, kAbrSlotIndexR, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURE(
      TestOsAbrSuccessfulBoot(kAbrSlotIndexB, kAbrSlotIndexR, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURE(
      TestOsAbrSuccessfulBoot(kAbrSlotIndexR, kAbrSlotIndexR, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURE(TestOsAbrSuccessfulBootOneShotRecovery(kForceRecoveryOn));
}

TEST(BootTests, SkipAddZbiItems) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  zircon_boot_ops.firmware_can_boot_kernel_slot = nullptr;
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
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  zircon_boot_ops.firmware_can_boot_kernel_slot = nullptr;
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
  ASSERT_NO_FATAL_FAILURE(ValidateBootedSlot(dev.get(), kAbrSlotIndexB));
  // Slot A unbootable
  AbrData abr_data;
  ASSERT_OK(dev->ReadFromPartition(GPT_DURABLE_BOOT_NAME, 0, sizeof(abr_data), &abr_data));
  ASSERT_EQ(abr_data.slot_data[0].tries_remaining, 0);
  ASSERT_EQ(abr_data.slot_data[0].successful_boot, 0);
}

TEST(BootTests, LoadAndBootInvalidZbiHeaderType) {
  ASSERT_NO_FATAL_FAILURE(TestInvalidZbiHeaderOsAbr([](auto hdr) { hdr->type = 0; }));
}

TEST(BootTests, LoadAndBootInvalidZbiHeaderExtra) {
  ASSERT_NO_FATAL_FAILURE(TestInvalidZbiHeaderOsAbr([](auto hdr) { hdr->extra = 0; }));
}

TEST(BootTests, LoadAndBootInvalidZbiHeaderMagic) {
  ASSERT_NO_FATAL_FAILURE(TestInvalidZbiHeaderOsAbr([](auto hdr) { hdr->magic = 0; }));
}

TEST(BootTests, LoadAndBootImageTooLarge) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  zircon_boot_ops.firmware_can_boot_kernel_slot = nullptr;
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
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  dev->SetFirmwareSlot(current_firmware_slot);
  MarkSlotActive(dev.get(), initial_active_slot);
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&zircon_boot_ops, buffer.data(), buffer.size(), force_recovery),
            kBootResultBootReturn);
  ASSERT_NO_FATAL_FAILURE(ValidateBootedSlot(dev.get(), current_firmware_slot));
}

TEST(BootTests, LoadAndBootMatchingSlotBootSucessful) {
  ASSERT_NO_FATAL_FAILURE(
      TestFirmareAbrMatchingSlotBootSucessful(kAbrSlotIndexA, kAbrSlotIndexA, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(
      TestFirmareAbrMatchingSlotBootSucessful(kAbrSlotIndexB, kAbrSlotIndexB, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(
      TestFirmareAbrMatchingSlotBootSucessful(kAbrSlotIndexR, kAbrSlotIndexR, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(
      TestFirmareAbrMatchingSlotBootSucessful(kAbrSlotIndexR, kAbrSlotIndexA, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURE(
      TestFirmareAbrMatchingSlotBootSucessful(kAbrSlotIndexR, kAbrSlotIndexB, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURE(
      TestFirmareAbrMatchingSlotBootSucessful(kAbrSlotIndexR, kAbrSlotIndexR, kForceRecoveryOn));
}

void TestFirmareAbrMatchingSlotBootSucessfulOneShotRecovery(AbrSlotIndex initial_active_slot) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  dev->SetFirmwareSlot(kAbrSlotIndexR);
  MarkSlotActive(dev.get(), initial_active_slot);
  AbrOps abr_ops = dev->GetAbrOps();
  AbrSetOneShotRecovery(&abr_ops, true);
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&zircon_boot_ops, buffer.data(), buffer.size(), kForceRecoveryOff),
            kBootResultBootReturn);
  ASSERT_NO_FATAL_FAILURE(ValidateBootedSlot(dev.get(), kAbrSlotIndexR));
}

TEST(BootTests, LoadAndBootMatchingSlotBootSucessfulOneShotRecovery) {
  ASSERT_NO_FATAL_FAILURE(TestFirmareAbrMatchingSlotBootSucessfulOneShotRecovery(kAbrSlotIndexA));
  ASSERT_NO_FATAL_FAILURE(TestFirmareAbrMatchingSlotBootSucessfulOneShotRecovery(kAbrSlotIndexB));
  ASSERT_NO_FATAL_FAILURE(TestFirmareAbrMatchingSlotBootSucessfulOneShotRecovery(kAbrSlotIndexR));
}

// Tests that device reboot if firmware slot doesn't matches the target slot to boot. i.e. either
// ABR metadata doesn't match firmware slot or force_recovery is on but device is not in firmware R
// slot.
void TestFirmwareAbrRebootIfSlotMismatched(AbrSlotIndex current_firmware_slot,
                                           AbrSlotIndex initial_active_slot,
                                           AbrSlotIndex expected_firmware_slot,
                                           ForceRecovery force_recovery) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
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
  ASSERT_NO_FATAL_FAILURE(TestFirmwareAbrRebootIfSlotMismatched(kAbrSlotIndexA, kAbrSlotIndexA,
                                                                kAbrSlotIndexR, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURE(TestFirmwareAbrRebootIfSlotMismatched(kAbrSlotIndexA, kAbrSlotIndexB,
                                                                kAbrSlotIndexB, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(TestFirmwareAbrRebootIfSlotMismatched(kAbrSlotIndexA, kAbrSlotIndexR,
                                                                kAbrSlotIndexR, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(TestFirmwareAbrRebootIfSlotMismatched(kAbrSlotIndexB, kAbrSlotIndexB,
                                                                kAbrSlotIndexR, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURE(TestFirmwareAbrRebootIfSlotMismatched(kAbrSlotIndexB, kAbrSlotIndexA,
                                                                kAbrSlotIndexA, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(TestFirmwareAbrRebootIfSlotMismatched(kAbrSlotIndexB, kAbrSlotIndexR,
                                                                kAbrSlotIndexR, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(TestFirmwareAbrRebootIfSlotMismatched(kAbrSlotIndexR, kAbrSlotIndexA,
                                                                kAbrSlotIndexA, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(TestFirmwareAbrRebootIfSlotMismatched(kAbrSlotIndexR, kAbrSlotIndexB,
                                                                kAbrSlotIndexB, kForceRecoveryOff));
}

// Tests that in the case of one shot recovery, device reboots if firmware slot doesn't match R.
void TestFirmwareAbrRebootIfSlotMismatchedOneShotRecovery(AbrSlotIndex current_firmware_slot) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps zircon_boot_ops = dev->GetZirconBootOps();
  dev->SetFirmwareSlot(current_firmware_slot);
  AbrOps abr_ops = dev->GetAbrOps();
  AbrSetOneShotRecovery(&abr_ops, true);
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&zircon_boot_ops, buffer.data(), buffer.size(), kForceRecoveryOff),
            kBootResultRebootReturn);
  ASSERT_FALSE(dev->GetBootedSlot());
  ASSERT_EQ(dev->GetFirmwareSlot(), kAbrSlotIndexR);
}

TEST(BootTests, LoadAndBootMismatchedSlotTriggerRebootOneShotRecovery) {
  ASSERT_NO_FATAL_FAILURE(TestFirmwareAbrRebootIfSlotMismatchedOneShotRecovery(kAbrSlotIndexA));
  ASSERT_NO_FATAL_FAILURE(TestFirmwareAbrRebootIfSlotMismatchedOneShotRecovery(kAbrSlotIndexB));
}

// Validate that a target slot is booted after successful kernel verification.
void ValidateVerifiedBootedSlot(const MockZirconBootOps* dev, AbrSlotIndex expected_slot) {
  auto booted_slot = dev->GetBootedSlot();
  ASSERT_TRUE(booted_slot);
  ASSERT_EQ(*booted_slot, expected_slot);

  std::multiset<NormalizedZbiItem> zbi_items_added;
  ASSERT_NO_FAILURES(ExtractAndSortZbiItems(dev->GetBootedImage().data(), &zbi_items_added));

  const std::string expected_cmdlines[] = {
      // Current slot item
      "zvb.current_slot=" + std::string(AbrGetSlotSuffix(expected_slot)),
      // Device zbi item
      kTestCmdline,
      // cmdline "vb_arg_1=foo_{slot}" from vbmeta property. See "generate_test_data.py"
      "vb_arg_1=foo" + std::string(AbrGetSlotSuffix(expected_slot)),
      // cmdline "vb_arg_2=bar_{slot}" from vbmeta property. See "generate_test_data.py"
      "vb_arg_2=bar" + std::string(AbrGetSlotSuffix(expected_slot)),
  };
  std::multiset<NormalizedZbiItem> zbi_items_expected;
  for (auto& str : expected_cmdlines) {
    zbi_items_expected.insert(NormalizeZbiItem(ZBI_TYPE_CMDLINE, 0, str.data(), str.size() + 1));
  }
  // Exactly the above items are appended. No more no less.
  EXPECT_EQ(zbi_items_added, zbi_items_expected);
}

// Test OS ABR logic pass verified boot logic and boot to expected slot.
void TestSuccessfulVerifiedBootOsAbr(AbrSlotIndex initial_active_slot, AbrSlotIndex expected_slot,
                                     ForceRecovery force_recovery) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps ops = dev->GetZirconBootOpsWithAvb();
  ops.firmware_can_boot_kernel_slot = nullptr;
  MarkSlotActive(dev.get(), initial_active_slot);
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&ops, buffer.data(), buffer.size(), force_recovery), kBootResultBootReturn);
  ASSERT_NO_FATAL_FAILURE(ValidateVerifiedBootedSlot(dev.get(), expected_slot));
}

TEST(BootTests, TestSuccessfulVerifiedBootOsAbr) {
  ASSERT_NO_FATAL_FAILURE(
      TestSuccessfulVerifiedBootOsAbr(kAbrSlotIndexA, kAbrSlotIndexA, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(
      TestSuccessfulVerifiedBootOsAbr(kAbrSlotIndexB, kAbrSlotIndexB, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(
      TestSuccessfulVerifiedBootOsAbr(kAbrSlotIndexR, kAbrSlotIndexR, kForceRecoveryOff));
  ASSERT_NO_FATAL_FAILURE(
      TestSuccessfulVerifiedBootOsAbr(kAbrSlotIndexA, kAbrSlotIndexR, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURE(
      TestSuccessfulVerifiedBootOsAbr(kAbrSlotIndexB, kAbrSlotIndexR, kForceRecoveryOn));
  ASSERT_NO_FATAL_FAILURE(
      TestSuccessfulVerifiedBootOsAbr(kAbrSlotIndexR, kAbrSlotIndexR, kForceRecoveryOn));
}

void CorruptSlots(MockZirconBootOps* dev, const std::vector<AbrSlotIndex>& corrupted_slots) {
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  for (auto slot : corrupted_slots) {
    // corrupt the slot
    const char* part = GetSlotPartitionName(slot);
    ASSERT_OK(dev->ReadFromPartition(part, 0, buffer.size(), buffer.data()));
    buffer[2 * sizeof(zbi_header_t)]++;
    ASSERT_OK(dev->WriteToPartition(part, 0, buffer.size(), buffer.data()));
  }
}

void VerifySlotMetadataUnbootable(MockZirconBootOps* dev, const std::vector<AbrSlotIndex>& slots) {
  AbrData abr_data;
  ASSERT_OK(dev->ReadFromPartition(GPT_DURABLE_BOOT_NAME, 0, sizeof(abr_data), &abr_data));
  for (auto slot : slots) {
    ASSERT_NE(slot, kAbrSlotIndexR);
    auto slot_data = slot == kAbrSlotIndexA ? abr_data.slot_data[0] : abr_data.slot_data[1];
    ASSERT_EQ(slot_data.tries_remaining, 0);
    ASSERT_EQ(slot_data.successful_boot, 0);
  }
}

// Test that OS ABR logic fall back to other slots when certain slots fail verification.
void TestVerifiedBootFailureOsAbr(const std::vector<AbrSlotIndex>& corrupted_slots,
                                  AbrSlotIndex initial_active_slot, AbrSlotIndex expected_slot) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps ops = dev->GetZirconBootOpsWithAvb();
  ops.firmware_can_boot_kernel_slot = nullptr;
  ASSERT_NO_FATAL_FAILURE(CorruptSlots(dev.get(), corrupted_slots));
  MarkSlotActive(dev.get(), initial_active_slot);
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&ops, buffer.data(), buffer.size(), kForceRecoveryOff),
            kBootResultBootReturn);
  ASSERT_NO_FATAL_FAILURE(ValidateVerifiedBootedSlot(dev.get(), expected_slot));
  ASSERT_NO_FATAL_FAILURE(VerifySlotMetadataUnbootable(dev.get(), corrupted_slots));
}

TEST(BootTests, LoadAndBootImageVerificationErrorFallBackOsAbr) {
  // Slot A fails, fall back to slot B
  ASSERT_NO_FATAL_FAILURE(
      TestVerifiedBootFailureOsAbr({kAbrSlotIndexA}, kAbrSlotIndexA, kAbrSlotIndexB));
  // Slot B fails, fall back to slot A.
  ASSERT_NO_FATAL_FAILURE(
      TestVerifiedBootFailureOsAbr({kAbrSlotIndexB}, kAbrSlotIndexB, kAbrSlotIndexA));
  // Slot A, B fail, slot A active, fall back to slot R.
  ASSERT_NO_FATAL_FAILURE(TestVerifiedBootFailureOsAbr({kAbrSlotIndexA, kAbrSlotIndexB},
                                                       kAbrSlotIndexA, kAbrSlotIndexR));
  // Slot A, B fail, slot B active, fall back to slot R.
  ASSERT_NO_FATAL_FAILURE(TestVerifiedBootFailureOsAbr({kAbrSlotIndexA, kAbrSlotIndexB},
                                                       kAbrSlotIndexB, kAbrSlotIndexR));
}

void TestVerifiedBootAllSlotsFailureOsAbr(AbrSlotIndex initial_active_slot) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps ops = dev->GetZirconBootOpsWithAvb();
  ops.firmware_can_boot_kernel_slot = nullptr;
  ASSERT_NO_FATAL_FAILURE(
      CorruptSlots(dev.get(), {kAbrSlotIndexA, kAbrSlotIndexB, kAbrSlotIndexR}));
  MarkSlotActive(dev.get(), initial_active_slot);
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&ops, buffer.data(), buffer.size(), kForceRecoveryOff),
            kBootResultErrorNoValidSlot);
  ASSERT_FALSE(dev->GetBootedSlot().has_value());
  ASSERT_NO_FATAL_FAILURE(
      VerifySlotMetadataUnbootable(dev.get(), {kAbrSlotIndexA, kAbrSlotIndexB}));
}

TEST(BootTests, LoadAndBootImageAllSlotVerificationError) {
  ASSERT_NO_FATAL_FAILURE(TestVerifiedBootAllSlotsFailureOsAbr(kAbrSlotIndexA));
  ASSERT_NO_FATAL_FAILURE(TestVerifiedBootAllSlotsFailureOsAbr(kAbrSlotIndexB));
  ASSERT_NO_FATAL_FAILURE(TestVerifiedBootAllSlotsFailureOsAbr(kAbrSlotIndexR));
}

// Tests that firmware ABR logic reboots if slot verification fails, except R slot.
void TestVerifiedBootFailureFirmwareAbr(AbrSlotIndex initial_active_slot,
                                        AbrSlotIndex expected_firmware_slot) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps ops = dev->GetZirconBootOpsWithAvb();

  // corrupt the image
  ASSERT_NO_FATAL_FAILURE(CorruptSlots(dev.get(), {initial_active_slot}));
  MarkSlotActive(dev.get(), initial_active_slot);
  dev->SetFirmwareSlot(initial_active_slot);
  // Slot failure, should trigger reboot into other slot.
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&ops, buffer.data(), buffer.size(), kForceRecoveryOff),
            kBootResultRebootReturn);
  ASSERT_FALSE(dev->GetBootedSlot());
  ASSERT_EQ(dev->GetFirmwareSlot(), expected_firmware_slot);

  AbrData abr_data;
  ASSERT_OK(dev->ReadFromPartition(GPT_DURABLE_BOOT_NAME, 0, sizeof(abr_data), &abr_data));
  // Failed slot should be marked unbootable.
  ASSERT_NO_FATAL_FAILURE(VerifySlotMetadataUnbootable(dev.get(), {initial_active_slot}));
}

TEST(BootTests, LoadAndBootFirmwareAbrVerificationError) {
  ASSERT_NO_FATAL_FAILURE(TestVerifiedBootFailureFirmwareAbr(kAbrSlotIndexA, kAbrSlotIndexB));
  ASSERT_NO_FATAL_FAILURE(TestVerifiedBootFailureFirmwareAbr(kAbrSlotIndexB, kAbrSlotIndexA));
}

TEST(BootTests, LoadAndBootFirmwareAbrRSlotVerificationError) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps ops = dev->GetZirconBootOpsWithAvb();

  // corrupt the image
  ASSERT_NO_FATAL_FAILURE(CorruptSlots(dev.get(), {kAbrSlotIndexR}));
  MarkSlotActive(dev.get(), kAbrSlotIndexR);
  dev->SetFirmwareSlot(kAbrSlotIndexR);
  // R Slot failure, should just return error without reboot.
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&ops, buffer.data(), buffer.size(), kForceRecoveryOff),
            kBootResultErrorNoValidSlot);
  ASSERT_FALSE(dev->GetBootedSlot());
}

TEST(BootTests, VerificationResultNotCheckedWhenUnlocked) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps ops = dev->GetZirconBootOpsWithAvb();
  ops.firmware_can_boot_kernel_slot = nullptr;
  // Set device unlocked.
  dev->SetDeviceLockStatus(MockZirconBootOps::LockStatus::kUnlocked);
  // Corrupt slot A
  ASSERT_NO_FATAL_FAILURE(CorruptSlots(dev.get(), {kAbrSlotIndexA}));
  // Boot to slot A.
  constexpr AbrSlotIndex active_slot = kAbrSlotIndexA;
  MarkSlotActive(dev.get(), kAbrSlotIndexA);
  // Boot should succeed.
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&ops, buffer.data(), buffer.size(), kForceRecoveryOff),
            kBootResultBootReturn);
  ASSERT_NO_FATAL_FAILURE(ValidateBootedSlot(dev.get(), active_slot));
}

TEST(BootTests, RollbackIndexUpdatedOnSuccessfulSlot) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps ops = dev->GetZirconBootOpsWithAvb();
  ops.firmware_can_boot_kernel_slot = nullptr;

  // Mark slot A successful.
  constexpr AbrSlotIndex active_slot = kAbrSlotIndexA;
  AbrOps abr_ops = dev->GetAbrOps();
  ASSERT_EQ(AbrMarkSlotSuccessful(&abr_ops, kAbrSlotIndexA), kAbrResultOk);
  ASSERT_EQ(AbrMarkSlotUnbootable(&abr_ops, kAbrSlotIndexB), kAbrResultOk);

  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&ops, buffer.data(), buffer.size(), kForceRecoveryOff),
            kBootResultBootReturn);
  ASSERT_NO_FATAL_FAILURE(ValidateVerifiedBootedSlot(dev.get(), active_slot));
  // Test vbmeta image A a has a rollback index of 5 at location 0. See generate_test_data.py.
  auto res = dev->ReadRollbackIndex(0);
  ASSERT_TRUE(res.is_ok());
  ASSERT_EQ(res.value(), 5);
}

TEST(BootTests, TestRollbackProtection) {
  std::unique_ptr<MockZirconBootOps> dev;
  ASSERT_NO_FATAL_FAILURE(CreateMockZirconBootOps(&dev));
  ZirconBootOps ops = dev->GetZirconBootOpsWithAvb();
  ops.firmware_can_boot_kernel_slot = nullptr;

  MarkSlotActive(dev.get(), kAbrSlotIndexA);
  // Slot A test vbmeta has rollback index 5 at location 0. Slot B test vbmeta has rollback index
  // 10. (See generate_test_data.py). With a rollback index of 7, slot A should fail, slot B should
  // succeed.
  dev->WriteRollbackIndex(0, 7);
  std::vector<uint8_t> buffer(kZirconPartitionSize);
  ASSERT_EQ(LoadAndBoot(&ops, buffer.data(), buffer.size(), kForceRecoveryOff),
            kBootResultBootReturn);
  ASSERT_NO_FATAL_FAILURE(ValidateVerifiedBootedSlot(dev.get(), kAbrSlotIndexB));
}
}  // namespace
