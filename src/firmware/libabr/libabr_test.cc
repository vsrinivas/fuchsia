// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/libabr.h"

#include "gtest/gtest.h"
#include "lib/abr_data.h"
#include "lib/abr_util.h"
#include "zlib.h"

namespace {

extern "C" {
// A CRC32 implementation for the test environment.
uint32_t AbrCrc32(const void* buf, size_t buf_size) {
  return crc32(0UL, reinterpret_cast<const uint8_t*>(buf), buf_size);
}

// AVBOps implementations which forward to a LibabrTest instance.
bool FakeReadAbrMetadata(void* context, size_t size, uint8_t* buffer);
bool FakeWriteAbrMetadata(void* context, const uint8_t* buffer, size_t size);
}  // extern "C"

// Call this after messing with metadata (if you want the CRC to match).
void UpdateMetadataCRC(AbrData* metadata) {
  metadata->crc32 = AbrHostToBigEndian(AbrCrc32(metadata, sizeof(*metadata) - sizeof(uint32_t)));
}

// Initializes metadata to a valid state where both slots are unbootable.
void InitializeMetadata(AbrData* metadata) {
  memset(metadata, 0, sizeof(*metadata));
  memcpy(metadata->magic, kAbrMagic, kAbrMagicLen);
  metadata->version_major = kAbrMajorVersion;
  metadata->version_minor = kAbrMinorVersion;
  UpdateMetadataCRC(metadata);
}

// Checks that metadata is valid and normalized. These conditions should always be true after
// libabr has updated the metadata, even if previous metadata was invalid.
void ValidateMetadata(const AbrData& metadata) {
  EXPECT_EQ(0, memcmp(metadata.magic, kAbrMagic, kAbrMagicLen));
  EXPECT_EQ(AbrBigEndianToHost(metadata.crc32),
            AbrCrc32(&metadata, sizeof(metadata) - sizeof(uint32_t)));
  EXPECT_EQ(kAbrMajorVersion, metadata.version_major);
  EXPECT_EQ(kAbrMinorVersion, metadata.version_minor);

  for (auto slot_index : {kAbrSlotIndexA, kAbrSlotIndexB}) {
    // If priority is zero, tries_remaining must also be zero.
    EXPECT_TRUE(metadata.slot_data[slot_index].priority > 0 ||
                metadata.slot_data[slot_index].tries_remaining == 0);

    // If priority is zero, successful_boot must also be zero.
    EXPECT_TRUE(metadata.slot_data[slot_index].priority > 0 ||
                metadata.slot_data[slot_index].successful_boot == 0);

    // If priority is not zero, tries_remaining and successful_boot must be consistent.
    EXPECT_TRUE(metadata.slot_data[slot_index].priority == 0 ||
                ((metadata.slot_data[slot_index].tries_remaining > 0) !=
                 (metadata.slot_data[slot_index].successful_boot > 0)));

    // Priority and tries_remaining must be in range.
    EXPECT_LE(metadata.slot_data[slot_index].priority, kAbrMaxPriority);
    EXPECT_LE(metadata.slot_data[slot_index].tries_remaining, kAbrMaxTriesRemaining);
  }
}

struct FakeOps {
  operator const AbrOps*() const { return &ops_; }

  // AbrOps calls to |read_abr_metadata| forward here.
  bool ReadMetadata(size_t size, uint8_t* buffer) {
    read_metadata_count_++;
    EXPECT_EQ(size, sizeof(AbrData));
    if (size != sizeof(AbrData)) {
      return false;
    }
    memcpy(buffer, &metadata_, sizeof(metadata_));
    return read_metadata_result_;
  }

  // AbrOps calls to |write_abr_metadata| forward here.
  bool WriteMetadata(const uint8_t* buffer, size_t size) {
    write_metadata_count_++;
    EXPECT_EQ(size, sizeof(AbrData));
    if (size != sizeof(AbrData)) {
      return false;
    }
    memcpy(&metadata_, buffer, sizeof(metadata_));
    return write_metadata_result_;
  }

  // Set these to false in a test to induce I/O errors.
  bool read_metadata_result_ = true;
  bool write_metadata_result_ = true;
  // These will be incremented on every AbrOps call from libabr.
  int read_metadata_count_ = 0;
  int write_metadata_count_ = 0;
  // This will be used as the 'stored' metadata for all AbrOps callbacks.
  AbrData metadata_{};
  // This will be used as the AbrOps argument for libabr calls.
  AbrOps ops_ = {this, FakeReadAbrMetadata, FakeWriteAbrMetadata};
};

FakeOps FakeOpsWithInitializedMetadata() {
  FakeOps ops;
  InitializeMetadata(&ops.metadata_);
  return ops;
}

AbrSlotIndex OtherSlot(AbrSlotIndex slot_index) {
  EXPECT_NE(kAbrSlotIndexR, slot_index);
  return (slot_index == kAbrSlotIndexA) ? kAbrSlotIndexB : kAbrSlotIndexA;
}

// These callbacks forward to a FakeOps instance.
bool FakeReadAbrMetadata(void* context, size_t size, uint8_t* buffer) {
  return reinterpret_cast<FakeOps*>(context)->ReadMetadata(size, buffer);
}

bool FakeWriteAbrMetadata(void* context, const uint8_t* buffer, size_t size) {
  return reinterpret_cast<FakeOps*>(context)->WriteMetadata(buffer, size);
}

TEST(LibabrTest, GetBootSlotNotInitialized) {
  FakeOps ops;
  memset(&ops.metadata_, 0, sizeof(ops.metadata_));
  EXPECT_EQ(kAbrSlotIndexA, AbrGetBootSlot(ops, true, nullptr));
  ValidateMetadata(ops.metadata_);
}

void GetBootSlotActiveNotSuccessful(AbrSlotIndex slot_index) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, slot_index));
  bool is_slot_marked_successful = true;
  EXPECT_EQ(slot_index, AbrGetBootSlot(ops, true, &is_slot_marked_successful));
  EXPECT_FALSE(is_slot_marked_successful);
  ValidateMetadata(ops.metadata_);
}
TEST(LibabrTest, GetBootSlotActiveNotSuccessfulA) {
  GetBootSlotActiveNotSuccessful(kAbrSlotIndexA);
}
TEST(LibabrTest, GetBootSlotActiveNotSuccessfulB) {
  GetBootSlotActiveNotSuccessful(kAbrSlotIndexB);
}

void GetBootSlotActiveSuccessful(AbrSlotIndex slot_index) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, slot_index));
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotSuccessful(ops, slot_index));
  bool is_slot_marked_successful = false;
  EXPECT_EQ(slot_index, AbrGetBootSlot(ops, true, &is_slot_marked_successful));
  EXPECT_TRUE(is_slot_marked_successful);
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, GetBootSlotActiveSuccessfulA) { GetBootSlotActiveSuccessful(kAbrSlotIndexA); }

TEST(LibabrTest, GetBootSlotActiveSuccessfulB) { GetBootSlotActiveSuccessful(kAbrSlotIndexB); }

TEST(LibabrTest, GetBootSlotNoBootableSlot) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrSlotIndexR, AbrGetBootSlot(ops, false, nullptr));
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, GetBootSlotNullReadOp) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.ops_.read_abr_metadata = nullptr;
  // The expectation is a fallback to recovery.
  EXPECT_EQ(kAbrSlotIndexR, AbrGetBootSlot(ops, true, nullptr));
}

TEST(LibabrTest, GetBootSlotNullWriteOpNoUpdate) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.ops_.write_abr_metadata = nullptr;
  EXPECT_EQ(kAbrSlotIndexA, AbrGetBootSlot(ops, false, nullptr));
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, GetBootSlotNullWriteOpUpdate) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.ops_.write_abr_metadata = nullptr;
  // The expectation is to ignore the write error.
  EXPECT_EQ(kAbrSlotIndexA, AbrGetBootSlot(ops, true, nullptr));
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, GetBootSlotReadIOError) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.read_metadata_result_ = false;
  // The expectation is a fallback to recovery.
  EXPECT_EQ(kAbrSlotIndexR, AbrGetBootSlot(ops, true, nullptr));
}

TEST(LibabrTest, GetBootSlotWriteIOErrorNoUpdate) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.write_metadata_result_ = false;
  EXPECT_EQ(kAbrSlotIndexA, AbrGetBootSlot(ops, false, nullptr));
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, GetBootSlotWriteIOErrorUpdate) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.write_metadata_result_ = false;
  // The expectation is to ignore the write error.
  EXPECT_EQ(kAbrSlotIndexA, AbrGetBootSlot(ops, true, nullptr));
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, GetBootSlotInvalidMetadataBadMagic) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexB));
  ops.metadata_.magic[0] = 'a';
  UpdateMetadataCRC(&ops.metadata_);
  // The expectation is that metadata is reinitialized, with A active.
  EXPECT_EQ(kAbrSlotIndexA, AbrGetBootSlot(ops, true, nullptr));
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, GetBootSlotInvalidMetadataBadCRC) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexB));
  ops.metadata_.crc32 = 0;
  // The expectation is that metadata is reinitialized, with A active.
  EXPECT_EQ(kAbrSlotIndexA, AbrGetBootSlot(ops, true, nullptr));
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, GetBootSlotInvalidMetadataUnsupportedVersion) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexB));
  ops.metadata_.version_major = 27;
  UpdateMetadataCRC(&ops.metadata_);
  // The expectation is a fallback to recovery without clobbering metadata.
  EXPECT_EQ(kAbrSlotIndexR, AbrGetBootSlot(ops, true, nullptr));
  EXPECT_EQ(ops.metadata_.version_major, 27);
}

TEST(LibabrTest, GetBootSlotInvalidMetadataLittleEndianCRC) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ops.metadata_.crc32 = AbrCrc32(&ops.metadata_, sizeof(ops.metadata_) - sizeof(uint32_t));
  // The expectation is that metadata is reinitialized, with A active.
  EXPECT_EQ(kAbrSlotIndexA, AbrGetBootSlot(ops, true, nullptr));
  ValidateMetadata(ops.metadata_);
}

void GetBootSlotNormalizeUnexpectedTries(AbrSlotIndex slot_index) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  // Set the metadata to a state where priority is zero, but tries remain.
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, slot_index));
  ops.metadata_.slot_data[slot_index].priority = 0;
  UpdateMetadataCRC(&ops.metadata_);
  EXPECT_EQ(kAbrSlotIndexR, AbrGetBootSlot(ops, true, nullptr));
  // The expectation is that the metadata has been normalized and updated.
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, GetBootSlotNormalizeUnexpectedTriesA) {
  GetBootSlotNormalizeUnexpectedTries(kAbrSlotIndexA);
}

TEST(LibabrTest, GetBootSlotNormalizeUnexpectedTriesB) {
  GetBootSlotNormalizeUnexpectedTries(kAbrSlotIndexB);
}

void GetBootSlotNormalizeUnexpectedSuccessMark(AbrSlotIndex slot_index) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  // Set the metadata to a state where priority is zero, but marked successful.
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, slot_index));
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotSuccessful(ops, slot_index));
  ops.metadata_.slot_data[slot_index].priority = 0;
  UpdateMetadataCRC(&ops.metadata_);
  EXPECT_EQ(kAbrSlotIndexR, AbrGetBootSlot(ops, true, nullptr));
  // The expectation is that the metadata has been normalized and updated.
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, GetBootSlotNormalizeUnexpectedSuccessMarkA) {
  GetBootSlotNormalizeUnexpectedSuccessMark(kAbrSlotIndexA);
}

TEST(LibabrTest, GetBootSlotNormalizeUnexpectedSuccessMarkB) {
  GetBootSlotNormalizeUnexpectedSuccessMark(kAbrSlotIndexB);
}

void GetBootSlotNormalizeTriesExhausted(AbrSlotIndex slot_index) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  // Set the metadata to a state where tries are exhausted and no successful mark.
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, slot_index));
  ops.metadata_.slot_data[slot_index].tries_remaining = 0;
  UpdateMetadataCRC(&ops.metadata_);
  EXPECT_EQ(kAbrSlotIndexR, AbrGetBootSlot(ops, true, nullptr));
  // The expectation is that the metadata has been normalized and updated.
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, GetBootSlotNormalizeTriesExhaustedA) {
  GetBootSlotNormalizeTriesExhausted(kAbrSlotIndexA);
}

TEST(LibabrTest, GetBootSlotNormalizeTriesExhaustedB) {
  GetBootSlotNormalizeTriesExhausted(kAbrSlotIndexB);
}

void GetBootSlotNormalizeSuccessfulWithUnexpectedTries(AbrSlotIndex slot_index) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  // Set the metadata to a state where tries remain alongside a successful mark.
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, slot_index));
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotSuccessful(ops, slot_index));
  ops.metadata_.slot_data[slot_index].tries_remaining = 3;
  UpdateMetadataCRC(&ops.metadata_);
  // Expect that the slot is reset to newly active state.
  bool is_slot_marked_successful = true;
  EXPECT_EQ(slot_index, AbrGetBootSlot(ops, true, &is_slot_marked_successful));
  EXPECT_FALSE(is_slot_marked_successful);
  // The expectation is that the metadata has been normalized and updated.
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, GetBootSlotNormalizeSuccessfulWithUnexpectedTriesA) {
  GetBootSlotNormalizeSuccessfulWithUnexpectedTries(kAbrSlotIndexA);
}

TEST(LibabrTest, GetBootSlotNormalizeSuccessfulWithUnexpectedTriesB) {
  GetBootSlotNormalizeSuccessfulWithUnexpectedTries(kAbrSlotIndexB);
}

void GetBootSlotNormalizePriorityOutOfRange(AbrSlotIndex slot_index) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  // Set the metadata to an active state where priority is higher than max.
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, slot_index));
  ops.metadata_.slot_data[slot_index].priority = kAbrMaxPriority + 1;
  UpdateMetadataCRC(&ops.metadata_);
  EXPECT_EQ(slot_index, AbrGetBootSlot(ops, true, nullptr));
  // The expectation is that the metadata has been normalized and updated.
  ValidateMetadata(ops.metadata_);

  // When at max, should not change.
  ops.metadata_.slot_data[slot_index].priority = kAbrMaxPriority;
  UpdateMetadataCRC(&ops.metadata_);
  AbrGetBootSlot(ops, true, nullptr);
  EXPECT_EQ(ops.metadata_.slot_data[slot_index].priority, kAbrMaxPriority);
}

TEST(LibabrTest, GetBootSlotNormalizePriorityOutOfRangeA) {
  GetBootSlotNormalizePriorityOutOfRange(kAbrSlotIndexA);
}

TEST(LibabrTest, GetBootSlotNormalizePriorityOutOfRangeB) {
  GetBootSlotNormalizePriorityOutOfRange(kAbrSlotIndexB);
}

void GetBootSlotNormalizeTriesOutOfRange(AbrSlotIndex slot_index) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  // Set the metadata to an active state where tries_remaining is higher than max.
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, slot_index));
  ops.metadata_.slot_data[slot_index].tries_remaining = kAbrMaxTriesRemaining + 1;
  UpdateMetadataCRC(&ops.metadata_);
  EXPECT_EQ(slot_index, AbrGetBootSlot(ops, true, nullptr));
  // The expectation is that the metadata has been normalized first and then the usual decrement.
  ValidateMetadata(ops.metadata_);
  EXPECT_EQ(ops.metadata_.slot_data[slot_index].tries_remaining, kAbrMaxTriesRemaining - 1);

  // When at max, should not change except for the usual decrement.
  ops.metadata_.slot_data[slot_index].tries_remaining = kAbrMaxTriesRemaining;
  UpdateMetadataCRC(&ops.metadata_);
  AbrGetBootSlot(ops, true, nullptr);
  EXPECT_EQ(ops.metadata_.slot_data[slot_index].tries_remaining, kAbrMaxTriesRemaining - 1);
}

TEST(LibabrTest, GetBootSlotNormalizeTriesOutOfRangeA) {
  GetBootSlotNormalizeTriesOutOfRange(kAbrSlotIndexA);
}

TEST(LibabrTest, GetBootSlotNormalizeTriesOutOfRangeB) {
  GetBootSlotNormalizeTriesOutOfRange(kAbrSlotIndexB);
}

TEST(LibabrTest, GetBootSlotOneShotRecovery) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexB));
  ASSERT_EQ(kAbrResultOk, AbrSetOneShotRecovery(ops, true));
  EXPECT_EQ(kAbrSlotIndexR, AbrGetBootSlot(ops, true, nullptr));
  ValidateMetadata(ops.metadata_);
  // The setting should be automatically reset.
  EXPECT_FALSE(ops.metadata_.one_shot_recovery_boot);
}

TEST(LibabrTest, GetBootSlotOneShotRecoveryNoUpdate) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexB));
  ASSERT_EQ(kAbrResultOk, AbrSetOneShotRecovery(ops, true));
  EXPECT_EQ(kAbrSlotIndexB, AbrGetBootSlot(ops, false, nullptr));
  ValidateMetadata(ops.metadata_);
  // The setting was ignored so should persist.
  EXPECT_TRUE(ops.metadata_.one_shot_recovery_boot);
}

TEST(LibabrTest, GetBootSlotUpdateTryCount) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexB));
  ops.metadata_.slot_data[kAbrSlotIndexB].tries_remaining = 3;
  UpdateMetadataCRC(&ops.metadata_);
  EXPECT_EQ(kAbrSlotIndexB, AbrGetBootSlot(ops, true, nullptr));
  ValidateMetadata(ops.metadata_);
  // Should be decremented by exactly one: 3 -> 2.
  EXPECT_EQ(2, ops.metadata_.slot_data[kAbrSlotIndexB].tries_remaining);
}

TEST(LibabrTest, GetBootSlotNoUpdates) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexB));
  ops.write_metadata_count_ = 0;
  EXPECT_EQ(kAbrSlotIndexB, AbrGetBootSlot(ops, false, nullptr));
  ValidateMetadata(ops.metadata_);
  EXPECT_EQ(0, ops.write_metadata_count_);
}

TEST(LibabrTest, GetBootSlotNoUpdatesFromNotInit) {
  FakeOps ops;
  memset(&ops.metadata_, 0, sizeof(ops.metadata_));
  ops.write_metadata_count_ = 0;
  EXPECT_EQ(kAbrSlotIndexA, AbrGetBootSlot(ops, false, nullptr));
  EXPECT_EQ(0, ops.write_metadata_count_);
}

TEST(LibabrTest, GetBootSlotNoUpdatesFromNotNormalized) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexB));
  ops.metadata_.slot_data[kAbrSlotIndexB].priority = 0;
  UpdateMetadataCRC(&ops.metadata_);
  ops.write_metadata_count_ = 0;
  EXPECT_EQ(kAbrSlotIndexR, AbrGetBootSlot(ops, false, nullptr));
  EXPECT_EQ(0, ops.write_metadata_count_);
}

TEST(LibabrTest, GetBootSlotNoExtraneousReads) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrSlotIndexR, AbrGetBootSlot(ops, false, nullptr));
  EXPECT_EQ(1, ops.read_metadata_count_);
}

TEST(LibabrTest, GetBootSlotNoExtraneousWrites) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotSuccessful(ops, kAbrSlotIndexA));
  ops.write_metadata_count_ = 0;
  EXPECT_EQ(kAbrSlotIndexA, AbrGetBootSlot(ops, true, nullptr));
  EXPECT_EQ(0, ops.write_metadata_count_);
}

TEST(LibabrTest, GetBootSlotNoExtraneousWritesOneUpdate) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.write_metadata_count_ = 0;
  EXPECT_EQ(kAbrSlotIndexA, AbrGetBootSlot(ops, true, nullptr));
  // Expecting an update because of the tries_remaining decrement, but should be just one.
  EXPECT_EQ(1, ops.write_metadata_count_);
}

TEST(LibabrTest, GetSlotSuffix) {
  EXPECT_EQ("_a", AbrGetSlotSuffix(kAbrSlotIndexA));
  EXPECT_EQ("_b", AbrGetSlotSuffix(kAbrSlotIndexB));
  EXPECT_EQ("_r", AbrGetSlotSuffix(kAbrSlotIndexR));
}

TEST(LibabrTest, GetSlotSuffixInvalidIndex) { EXPECT_EQ("", AbrGetSlotSuffix((AbrSlotIndex)-1)); }

void MarkSlotActive(AbrSlotIndex slot_index) {
  AbrSlotIndex other_slot_index = OtherSlot(slot_index);
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, slot_index));
  EXPECT_GT(ops.metadata_.slot_data[slot_index].priority, 0);
  EXPECT_GT(ops.metadata_.slot_data[slot_index].tries_remaining, 0);
  EXPECT_EQ(ops.metadata_.slot_data[slot_index].successful_boot, 0);
  EXPECT_EQ(ops.metadata_.slot_data[other_slot_index].priority, 0);
  EXPECT_EQ(ops.metadata_.slot_data[other_slot_index].tries_remaining, 0);
  EXPECT_EQ(ops.metadata_.slot_data[other_slot_index].successful_boot, 0);
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, MarkSlotActiveA) { MarkSlotActive(kAbrSlotIndexA); }

TEST(LibabrTest, MarkSlotActiveB) { MarkSlotActive(kAbrSlotIndexB); }

void MarkSlotActiveOverOther(AbrSlotIndex slot_index) {
  AbrSlotIndex other_slot_index = OtherSlot(slot_index);
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, other_slot_index));
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotSuccessful(ops, other_slot_index));
  EXPECT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, slot_index));
  EXPECT_GT(ops.metadata_.slot_data[slot_index].priority,
            ops.metadata_.slot_data[other_slot_index].priority);
  EXPECT_GT(ops.metadata_.slot_data[slot_index].tries_remaining, 0);
  EXPECT_EQ(ops.metadata_.slot_data[slot_index].successful_boot, 0);
  EXPECT_GT(ops.metadata_.slot_data[other_slot_index].priority, 0);
  EXPECT_EQ(ops.metadata_.slot_data[other_slot_index].tries_remaining, 0);
  EXPECT_EQ(ops.metadata_.slot_data[other_slot_index].successful_boot, 1);
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, MarkSlotActiveOverOtherA) { MarkSlotActiveOverOther(kAbrSlotIndexA); }

TEST(LibabrTest, MarkSlotActiveOverOtherB) { MarkSlotActiveOverOther(kAbrSlotIndexB); }

TEST(LibabrTest, MarkSlotActiveR) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrResultErrorInvalidData, AbrMarkSlotActive(ops, kAbrSlotIndexR));
}

TEST(LibabrTest, MarkSlotActiveInvalidIndex) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrResultErrorInvalidData, AbrMarkSlotActive(ops, (AbrSlotIndex)-1));
}

TEST(LibabrTest, MarkSlotActiveReadFailure) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ops.read_metadata_result_ = false;
  EXPECT_EQ(kAbrResultErrorIo, AbrMarkSlotActive(ops, kAbrSlotIndexA));
}

TEST(LibabrTest, MarkSlotActiveWriteFailure) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ops.write_metadata_result_ = false;
  EXPECT_EQ(kAbrResultErrorIo, AbrMarkSlotActive(ops, kAbrSlotIndexA));
}

TEST(LibabrTest, MarkSlotActiveNoExtraneousReads) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  EXPECT_EQ(1, ops.read_metadata_count_);
}

TEST(LibabrTest, MarkSlotActiveNoExtraneousWrites) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  EXPECT_EQ(1, ops.write_metadata_count_);
  ops.write_metadata_count_ = 0;
  EXPECT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  EXPECT_EQ(0, ops.write_metadata_count_);
}

void MarkSlotUnbootable(AbrSlotIndex slot_index) {
  AbrSlotIndex other_slot_index = OtherSlot(slot_index);
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, slot_index));
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotSuccessful(ops, slot_index));
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, other_slot_index));
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotSuccessful(ops, other_slot_index));
  EXPECT_EQ(kAbrResultOk, AbrMarkSlotUnbootable(ops, slot_index));
  EXPECT_EQ(ops.metadata_.slot_data[slot_index].priority, 0);
  EXPECT_EQ(ops.metadata_.slot_data[slot_index].tries_remaining, 0);
  EXPECT_EQ(ops.metadata_.slot_data[slot_index].successful_boot, 0);
  EXPECT_GT(ops.metadata_.slot_data[other_slot_index].priority, 0);
  EXPECT_EQ(ops.metadata_.slot_data[other_slot_index].tries_remaining, 0);
  EXPECT_GT(ops.metadata_.slot_data[other_slot_index].successful_boot, 0);
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, MarkSlotUnbootableA) { MarkSlotUnbootable(kAbrSlotIndexA); }

TEST(LibabrTest, MarkSlotUnbootableB) { MarkSlotUnbootable(kAbrSlotIndexB); }

TEST(LibabrTest, MarkSlotUnbootableR) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrResultErrorInvalidData, AbrMarkSlotUnbootable(ops, kAbrSlotIndexR));
}

TEST(LibabrTest, MarkSlotUnbootableInvalidIndex) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrResultErrorInvalidData, AbrMarkSlotUnbootable(ops, (AbrSlotIndex)-1));
}

TEST(LibabrTest, MarkSlotUnbootableReadFailure) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.read_metadata_result_ = false;
  EXPECT_EQ(kAbrResultErrorIo, AbrMarkSlotUnbootable(ops, kAbrSlotIndexA));
}

TEST(LibabrTest, MarkSlotUnbootableWriteFailure) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.write_metadata_result_ = false;
  EXPECT_EQ(kAbrResultErrorIo, AbrMarkSlotUnbootable(ops, kAbrSlotIndexA));
}

TEST(LibabrTest, MarkSlotUnbootableNoExtraneousReads) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.read_metadata_count_ = 0;
  EXPECT_EQ(kAbrResultOk, AbrMarkSlotUnbootable(ops, kAbrSlotIndexA));
  EXPECT_EQ(1, ops.read_metadata_count_);
}

TEST(LibabrTest, MarkSlotUnbootableNoExtraneousWrites) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.write_metadata_count_ = 0;
  EXPECT_EQ(kAbrResultOk, AbrMarkSlotUnbootable(ops, kAbrSlotIndexA));
  EXPECT_EQ(1, ops.write_metadata_count_);
  ops.write_metadata_count_ = 0;
  EXPECT_EQ(kAbrResultOk, AbrMarkSlotUnbootable(ops, kAbrSlotIndexA));
  EXPECT_EQ(0, ops.write_metadata_count_);
}

void MarkSlotSuccessful(AbrSlotIndex slot_index) {
  AbrSlotIndex other_slot_index = OtherSlot(slot_index);
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, slot_index));
  EXPECT_EQ(kAbrResultOk, AbrMarkSlotSuccessful(ops, slot_index));
  EXPECT_GT(ops.metadata_.slot_data[slot_index].priority, 0);
  EXPECT_EQ(ops.metadata_.slot_data[slot_index].tries_remaining, 0);
  EXPECT_GT(ops.metadata_.slot_data[slot_index].successful_boot, 0);
  EXPECT_EQ(ops.metadata_.slot_data[other_slot_index].priority, 0);
  EXPECT_EQ(ops.metadata_.slot_data[other_slot_index].tries_remaining, 0);
  EXPECT_EQ(ops.metadata_.slot_data[other_slot_index].successful_boot, 0);
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, MarkSlotSuccessfulA) { MarkSlotSuccessful(kAbrSlotIndexA); }

TEST(LibabrTest, MarkSlotSuccessfulB) { MarkSlotSuccessful(kAbrSlotIndexB); }

TEST(LibabrTest, MarkSlotSuccessfulR) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrResultErrorInvalidData, AbrMarkSlotSuccessful(ops, kAbrSlotIndexR));
}

TEST(LibabrTest, MarkSlotSuccessfulInvalidIndex) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrResultErrorInvalidData, AbrMarkSlotSuccessful(ops, (AbrSlotIndex)-1));
}

TEST(LibabrTest, MarkSlotSuccessfulUnbootable) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrResultErrorInvalidData, AbrMarkSlotSuccessful(ops, kAbrSlotIndexA));
}

TEST(LibabrTest, MarkSlotSuccessfulReadFailure) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.read_metadata_result_ = false;
  EXPECT_EQ(kAbrResultErrorIo, AbrMarkSlotSuccessful(ops, kAbrSlotIndexA));
}

TEST(LibabrTest, MarkSlotSuccessfulWriteFailure) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.write_metadata_result_ = false;
  EXPECT_EQ(kAbrResultErrorIo, AbrMarkSlotSuccessful(ops, kAbrSlotIndexA));
}

TEST(LibabrTest, MarkSlotSuccessfulNoExtraneousReads) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.read_metadata_count_ = 0;
  EXPECT_EQ(kAbrResultOk, AbrMarkSlotSuccessful(ops, kAbrSlotIndexA));
  EXPECT_EQ(1, ops.read_metadata_count_);
}

TEST(LibabrTest, MarkSlotSuccessfulNoExtraneousWrites) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ops.write_metadata_count_ = 0;
  EXPECT_EQ(kAbrResultOk, AbrMarkSlotSuccessful(ops, kAbrSlotIndexA));
  EXPECT_EQ(1, ops.write_metadata_count_);
  ops.write_metadata_count_ = 0;
  EXPECT_EQ(kAbrResultOk, AbrMarkSlotSuccessful(ops, kAbrSlotIndexA));
  EXPECT_EQ(0, ops.write_metadata_count_);
}

void GetSlotInfo(AbrSlotIndex slot_index) {
  AbrSlotIndex other_slot_index = OtherSlot(slot_index);
  FakeOps ops = FakeOpsWithInitializedMetadata();
  AbrSlotInfo info;
  ASSERT_EQ(kAbrResultOk, AbrGetSlotInfo(ops, slot_index, &info));
  EXPECT_FALSE(info.is_bootable);
  EXPECT_FALSE(info.is_active);
  EXPECT_FALSE(info.is_marked_successful);
  EXPECT_EQ(info.num_tries_remaining, 0);
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, slot_index));
  ASSERT_EQ(kAbrResultOk, AbrGetSlotInfo(ops, slot_index, &info));
  EXPECT_TRUE(info.is_bootable);
  EXPECT_TRUE(info.is_active);
  EXPECT_FALSE(info.is_marked_successful);
  EXPECT_GT(info.num_tries_remaining, 0);
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotSuccessful(ops, slot_index));
  ASSERT_EQ(kAbrResultOk, AbrGetSlotInfo(ops, slot_index, &info));
  EXPECT_TRUE(info.is_bootable);
  EXPECT_TRUE(info.is_active);
  EXPECT_TRUE(info.is_marked_successful);
  EXPECT_EQ(info.num_tries_remaining, 0);
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, other_slot_index));
  ASSERT_EQ(kAbrResultOk, AbrGetSlotInfo(ops, slot_index, &info));
  EXPECT_TRUE(info.is_bootable);
  EXPECT_FALSE(info.is_active);
  EXPECT_TRUE(info.is_marked_successful);
  EXPECT_EQ(info.num_tries_remaining, 0);
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotUnbootable(ops, slot_index));
  ASSERT_EQ(kAbrResultOk, AbrGetSlotInfo(ops, slot_index, &info));
  EXPECT_FALSE(info.is_bootable);
  EXPECT_FALSE(info.is_active);
  EXPECT_FALSE(info.is_marked_successful);
  EXPECT_EQ(info.num_tries_remaining, 0);
}

TEST(LibabrTest, GetSlotInfoA) { GetSlotInfo(kAbrSlotIndexA); }

TEST(LibabrTest, GetSlotInfoB) { GetSlotInfo(kAbrSlotIndexB); }

TEST(LibabrTest, GetSlotInfoR) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  AbrSlotInfo info;
  ASSERT_EQ(kAbrResultOk, AbrGetSlotInfo(ops, kAbrSlotIndexR, &info));
  EXPECT_TRUE(info.is_bootable);
  EXPECT_TRUE(info.is_active);
  EXPECT_TRUE(info.is_marked_successful);
  EXPECT_EQ(info.num_tries_remaining, 0);
  // When any other slot is bootable, R is not active.
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexB));
  ASSERT_EQ(kAbrResultOk, AbrGetSlotInfo(ops, kAbrSlotIndexR, &info));
  EXPECT_TRUE(info.is_bootable);
  EXPECT_FALSE(info.is_active);
  EXPECT_TRUE(info.is_marked_successful);
  EXPECT_EQ(info.num_tries_remaining, 0);
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotUnbootable(ops, kAbrSlotIndexB));
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexA));
  ASSERT_EQ(kAbrResultOk, AbrGetSlotInfo(ops, kAbrSlotIndexR, &info));
  EXPECT_TRUE(info.is_bootable);
  EXPECT_FALSE(info.is_active);
  EXPECT_TRUE(info.is_marked_successful);
  EXPECT_EQ(info.num_tries_remaining, 0);
  ASSERT_EQ(kAbrResultOk, AbrMarkSlotActive(ops, kAbrSlotIndexB));
  EXPECT_TRUE(info.is_bootable);
  EXPECT_FALSE(info.is_active);
  EXPECT_TRUE(info.is_marked_successful);
  EXPECT_EQ(info.num_tries_remaining, 0);
}

TEST(LibabrTest, GetSlotInfoReadFailure) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ops.read_metadata_result_ = false;
  AbrSlotInfo info;
  EXPECT_EQ(kAbrResultErrorIo, AbrGetSlotInfo(ops, kAbrSlotIndexB, &info));
}

TEST(LibabrTest, GetSlotInfoNoExtraneousReads) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ops.read_metadata_count_ = 0;
  AbrSlotInfo info;
  EXPECT_EQ(kAbrResultOk, AbrGetSlotInfo(ops, kAbrSlotIndexB, &info));
  EXPECT_EQ(1, ops.read_metadata_count_);
}

TEST(LibabrTest, GetSlotInfoNoWrites) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  AbrSlotInfo info;
  EXPECT_EQ(kAbrResultOk, AbrGetSlotInfo(ops, kAbrSlotIndexB, &info));
  EXPECT_EQ(0, ops.write_metadata_count_);
}

TEST(LibabrTest, SetOneShotRecovery) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrResultOk, AbrSetOneShotRecovery(ops, true));
  EXPECT_EQ(1, ops.metadata_.one_shot_recovery_boot);
  ValidateMetadata(ops.metadata_);
  EXPECT_EQ(kAbrResultOk, AbrSetOneShotRecovery(ops, false));
  EXPECT_EQ(0, ops.metadata_.one_shot_recovery_boot);
  ValidateMetadata(ops.metadata_);
}

TEST(LibabrTest, SetOneShotRecoveryReadFailure) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ops.read_metadata_result_ = false;
  EXPECT_EQ(kAbrResultErrorIo, AbrSetOneShotRecovery(ops, true));
}

TEST(LibabrTest, SetOneShotRecoveryWriteFailure) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  ops.write_metadata_result_ = false;
  EXPECT_EQ(kAbrResultErrorIo, AbrSetOneShotRecovery(ops, true));
}

TEST(LibabrTest, SetOneShotRecoveryNoExtraneousReads) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrResultOk, AbrSetOneShotRecovery(ops, true));
  EXPECT_EQ(1, ops.read_metadata_count_);
}

TEST(LibabrTest, SetOneShotRecoveryNoExtraneousWrites) {
  FakeOps ops = FakeOpsWithInitializedMetadata();
  EXPECT_EQ(kAbrResultOk, AbrSetOneShotRecovery(ops, true));
  EXPECT_EQ(1, ops.write_metadata_count_);
  ops.write_metadata_count_ = 0;
  EXPECT_EQ(kAbrResultOk, AbrSetOneShotRecovery(ops, true));
  EXPECT_EQ(0, ops.write_metadata_count_);
}

}  // namespace
