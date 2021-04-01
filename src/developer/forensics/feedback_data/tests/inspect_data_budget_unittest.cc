// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/inspect_data_budget.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics {
namespace feedback_data {
namespace {

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

  void SetBudget(size_t zip_file_kb) {
    std::map<std::string, ArchiveFileStats> file_size_stats;

    // The Inspect file must exists or else the inspect budget is disabled.
    file_size_stats["inspect.json"] = {0, 0};
    file_size_stats["other"] = {0, zip_file_kb * 1024};

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
  SetBudget(1024);
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
  ASSERT_TRUE(GetSizeInBytes());
  size_t initial_budget = GetSizeInBytes().value();

  SetBudget(2048);
  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), initial_budget);
}

TEST_F(InspectDataBudgetTest, TestSizeBudget_UpperLimit) {
  MakeLimitedBudget();
  ASSERT_TRUE(GetSizeInBytes());
  size_t initial_budget = GetSizeInBytes().value();
  SetBudget(724);

  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), initial_budget);
}

TEST_F(InspectDataBudgetTest, TestSizeBudget_LowerLimit) {
  // Arrive at the lower limit by making the zip size 2 GB twice (this should reduce the initial
  // budget at most by 2^16 times).
  MakeLimitedBudget();
  SetBudget(2 * 1024 * 1024);
  SetBudget(2 * 1024 * 1024);
  ASSERT_TRUE(GetSizeInBytes());
  size_t lower_limit = GetSizeInBytes().value();

  SetBudget(1024 * 1024);
  ASSERT_TRUE(GetSizeInBytes());
  size_t new_budget = GetSizeInBytes().value();

  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(lower_limit, new_budget);
}

TEST_F(InspectDataBudgetTest, TestSizeBudget_Reduce_Increase) {
  MakeLimitedBudget();
  ASSERT_TRUE(GetSizeInBytes());
  size_t initial_budget = GetSizeInBytes().value();
  size_t budget = (initial_budget * 1024) / 1500;

  SetBudget(3000);
  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), budget);

  // Note: Make sure that the geometric mean of the last zip size and the new zip size > 2MB.
  // Otherwise the resulting budget might be lower than our calculated value due to upper limit
  // restrictions.
  budget = (budget * 1024) / 800;
  SetBudget(1600);
  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), budget);
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
