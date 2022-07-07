// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <random>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

constexpr uint64_t kDefaultBlockCount = 262144;
class GcManagerTest : public F2fsFakeDevTestFixture {
 public:
  GcManagerTest(TestOptions options = TestOptions{.block_count = kDefaultBlockCount})
      : F2fsFakeDevTestFixture(options) {}

 protected:
  std::vector<std::string> MakeGcTriggerCondition() {
    std::random_device random_device;
    std::mt19937 prng(random_device());

    fs_->GetGcManager().DisableFgGc();
    std::vector<std::string> total_file_names;
    uint32_t count = 0;
    while (true) {
      if (fs_->GetSegmentManager().HasNotEnoughFreeSecs()) {
        break;
      }
      std::vector<std::string> file_names;
      for (uint32_t i = 0; i < fs_->GetSuperblockInfo().GetBlocksPerSeg(); ++i, ++count) {
        std::string file_name = std::to_string(count);
        fbl::RefPtr<fs::Vnode> test_file;
        EXPECT_EQ(root_dir_->Create(file_name, S_IFREG, &test_file), ZX_OK);
        file_names.push_back(file_name);
        EXPECT_EQ(test_file->Close(), ZX_OK);
      }
      fs_->WriteCheckpoint(false, false);

      std::shuffle(file_names.begin(), file_names.end(), prng);

      const uint64_t kDeleteSize = file_names.size() / 4;
      auto iter = file_names.begin();
      for (uint64_t i = 0; i < kDeleteSize; ++i) {
        EXPECT_EQ(root_dir_->Unlink(*iter, false), ZX_OK);
        iter = file_names.erase(iter);
      }
      fs_->WriteCheckpoint(false, false);
      total_file_names.insert(total_file_names.end(), file_names.begin(), file_names.end());
    }

    fs_->GetGcManager().EnableFgGc();
    return total_file_names;
  }
};

TEST_F(GcManagerTest, CpError) {
  fs_->GetSuperblockInfo().SetCpFlags(CpFlag::kCpErrorFlag);
  auto result = fs_->GetGcManager().F2fsGc();
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error_value(), ZX_ERR_BAD_STATE);
}

class GcManagerTestWithLargeSec
    : public GcManagerTest,
      public testing::WithParamInterface<std::pair<uint64_t, uint32_t>> {
 public:
  GcManagerTestWithLargeSec()
      : GcManagerTest(TestOptions{.block_count = GetParam().first,
                                  .mkfs_options = MkfsOptions{.segs_per_sec = GetParam().second}}) {
  }
};

TEST_P(GcManagerTestWithLargeSec, SegmentDirtyInfo) {
  MakeGcTriggerCondition();
  DirtySeglistInfo *dirty_info = &fs_->GetSegmentManager().GetDirtySegmentInfo();

  // Get Victim
  uint32_t last_victim =
      fs_->GetSuperblockInfo().GetLastVictim(static_cast<int>(GcMode::kGcGreedy));
  auto victim_seg_or = fs_->GetSegmentManager().GetVictimByDefault(
      GcType::kFgGc, CursegType::kNoCheckType, AllocMode::kLFS);
  ASSERT_FALSE(victim_seg_or.is_error());
  uint32_t victim_seg = victim_seg_or.value();
  fs_->GetSuperblockInfo().SetLastVictim(static_cast<int>(GcMode::kGcGreedy), last_victim);
  fs_->GetGcManager().SetCurVictimSec(kNullSecNo);

  // Check at least one of victim seg is dirty
  bool is_dirty = false;
  const uint32_t start_segno = victim_seg - (victim_seg % fs_->GetSuperblockInfo().GetSegsPerSec());
  for (uint32_t i = 0; i < fs_->GetSuperblockInfo().GetSegsPerSec(); ++i) {
    is_dirty |= TestBit(start_segno + i,
                        dirty_info->dirty_segmap[static_cast<int>(DirtyType::kDirty)].get());
  }
  ASSERT_TRUE(is_dirty);

  // Copy prev nr_dirty info
  int prev_nr_dirty[static_cast<int>(DirtyType::kNrDirtytype)] = {};
  memcpy(prev_nr_dirty, dirty_info->nr_dirty, sizeof(prev_nr_dirty));

  // Trigger GC
  auto result = fs_->GetGcManager().F2fsGc();
  ASSERT_FALSE(result.is_error());

  // Check victim seg is clean
  for (uint32_t i = 0; i < fs_->GetSuperblockInfo().GetSegsPerSec(); ++i) {
    ASSERT_FALSE(TestBit(start_segno + i,
                         dirty_info->dirty_segmap[static_cast<int>(DirtyType::kDirty)].get()));
  }

  // Check nr_dirty decreased
  for (int i = static_cast<int>(DirtyType::kDirtyHotData); i <= static_cast<int>(DirtyType::kDirty);
       ++i) {
    ASSERT_TRUE(dirty_info->nr_dirty[i] <= prev_nr_dirty[i]);
  }
}

TEST_P(GcManagerTestWithLargeSec, SegmentFreeInfo) {
  MakeGcTriggerCondition();
  FreeSegmapInfo *free_info = &fs_->GetSegmentManager().GetFreeSegmentInfo();

  // Get Victim
  uint32_t last_victim =
      fs_->GetSuperblockInfo().GetLastVictim(static_cast<int>(GcMode::kGcGreedy));
  auto victim_seg_or = fs_->GetSegmentManager().GetVictimByDefault(
      GcType::kFgGc, CursegType::kNoCheckType, AllocMode::kLFS);
  ASSERT_FALSE(victim_seg_or.is_error());
  uint32_t victim_seg = victim_seg_or.value();
  fs_->GetSuperblockInfo().SetLastVictim(static_cast<int>(GcMode::kGcGreedy), last_victim);
  fs_->GetGcManager().SetCurVictimSec(kNullSecNo);
  uint32_t victim_sec = fs_->GetSegmentManager().GetSecNo(victim_seg);

  // Check victim sec is not free
  ASSERT_TRUE(TestBit(victim_sec, free_info->free_secmap.get()));

  // Trigger GC
  auto result = fs_->GetGcManager().F2fsGc();
  ASSERT_FALSE(result.is_error());

  // Check victim sec is freed
  ASSERT_FALSE(TestBit(victim_sec, free_info->free_secmap.get()));
}

TEST_P(GcManagerTestWithLargeSec, NodeGcConsistency) {
  std::vector<std::string> file_names = MakeGcTriggerCondition();

  while (true) {
    auto result = fs_->GetGcManager().F2fsGc();
    // no victim
    if (result.is_error() && result.error_value() == ZX_ERR_UNAVAILABLE) {
      break;
    }
    ASSERT_FALSE(result.is_error());
  }

  for (auto name : file_names) {
    fbl::RefPtr<fs::Vnode> vn;
    FileTester::Lookup(root_dir_.get(), name, &vn);
    ASSERT_TRUE(vn);
    vn->Close();
  }
}

const std::array<std::pair<uint64_t, uint32_t>, 3> kSecParams = {
    {{kDefaultBlockCount, 1}, {kDefaultBlockCount, 2}, {2 * kDefaultBlockCount, 4}}};
INSTANTIATE_TEST_SUITE_P(GcManagerTestWithLargeSec, GcManagerTestWithLargeSec,
                         ::testing::ValuesIn(kSecParams));

}  // namespace
}  // namespace f2fs
