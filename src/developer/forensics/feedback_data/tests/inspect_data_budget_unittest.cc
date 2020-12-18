// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/inspect_data_budget.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics {
namespace feedback_data {
namespace {

const size_t kOneKb = 1024;

class InspectDataBudgetTest : public UnitTestFixture {
 public:
  void MakeUnlimitedBudget() {
    inspect_data_budget_ = std::make_unique<InspectDataBudget>("non-existent_path");
  }

  void MakeLimitedBudget() {
    std::string limit_data_flag_path = files::JoinPath(tmp_dir_.path(), "limit_inspect_data");
    files::WriteFile(limit_data_flag_path, " ");
    inspect_data_budget_ = std::make_unique<InspectDataBudget>(limit_data_flag_path.c_str());
  }

  void SetBudget(size_t zip_file_kb, size_t inspect_raw_size_kb) {
    std::map<std::string, ArchiveFileStats> file_size_stats;

    // Shift right by one for compression ratio = 2.
    size_t inspect_compressed_size_kb = inspect_raw_size_kb >> 1;
    file_size_stats["inspect.json"] = {inspect_raw_size_kb * kOneKb,
                                       inspect_compressed_size_kb * kOneKb};
    file_size_stats["other"] = {0, (zip_file_kb - inspect_compressed_size_kb) * kOneKb};

    inspect_data_budget_->UpdateBudget(file_size_stats);
  }

  void SetBudget(const std::map<std::string, ArchiveFileStats>& file_size_stats) {
    inspect_data_budget_->UpdateBudget(file_size_stats);
  }

  std::optional<size_t> GetSizeInBytes() { return inspect_data_budget_->SizeInBytes(); }

 private:
  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<InspectDataBudget> inspect_data_budget_;
};

TEST_F(InspectDataBudgetTest, TestUnlimitedBudget) {
  MakeUnlimitedBudget();
  ASSERT_FALSE(GetSizeInBytes());

  // setting a budget should not do anything.
  SetBudget(1024, 100);
  ASSERT_FALSE(GetSizeInBytes());
}

TEST_F(InspectDataBudgetTest, TestLimitedBudget) {
  MakeLimitedBudget();
  ASSERT_TRUE(GetSizeInBytes());
}

TEST_F(InspectDataBudgetTest, TestForCrash_MissingSizeStats) {
  MakeLimitedBudget();
  std::map<std::string, ArchiveFileStats> file_size_stats;
  SetBudget(file_size_stats);
}

TEST_F(InspectDataBudgetTest, TestSizeBudget_Maintain) {
  MakeLimitedBudget();
  SetBudget(1024, 100);
  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), static_cast<size_t>(100 * 1024));
}

TEST_F(InspectDataBudgetTest, TestSizeBudget_Increase) {
  MakeLimitedBudget();
  SetBudget(724, 100);
  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), static_cast<size_t>(500 * 1024));
}

TEST_F(InspectDataBudgetTest, TestSizeBudget_Reduce) {
  MakeLimitedBudget();
  SetBudget(1054, 100);
  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), static_cast<size_t>(60 * 1024));
}

TEST_F(InspectDataBudgetTest, TestSizeBudget_Underflow) {
  MakeLimitedBudget();
  SetBudget(1324, 100);
  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), static_cast<size_t>(0));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
