// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/inspect_data_budget.h"

#include <algorithm>
#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics {
namespace feedback_data {
namespace {

constexpr size_t kKilobytes = 1024u;
constexpr size_t kMegabytes = 1048576u;
constexpr size_t kGigabytes = 1073741824u;

using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using inspect::testing::UintArrayIs;
using inspect::testing::UintIs;
using testing::AllOf;
using testing::IsEmpty;
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

class InspectDataBudgetTest : public UnitTestFixture {
 public:
  void SetUp() override {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    cobalt_ = std::make_unique<cobalt::Logger>(dispatcher(), services());
  }

  void MakeUnlimitedBudget() {
    inspect_node_manager_ = std::make_unique<InspectNodeManager>(&InspectRoot());
    inspect_data_budget_ = std::make_unique<InspectDataBudget>(
        "non-existent_path", inspect_node_manager_.get(), cobalt_.get());
  }

  void MakeLimitedBudget() {
    inspect_node_manager_ = std::make_unique<InspectNodeManager>(&InspectRoot());

    std::string limit_data_flag_path = files::JoinPath(tmp_dir_.path(), "limit_inspect_data");
    files::WriteFile(limit_data_flag_path, " ");
    inspect_data_budget_ = std::make_unique<InspectDataBudget>(
        limit_data_flag_path.c_str(), inspect_node_manager_.get(), cobalt_.get());
  }

  void CalcBudget(size_t zip_file_bytes) {
    std::map<std::string, ArchiveFileStats> file_size_stats;

    // The Inspect file must exists or else the inspect budget is disabled.
    file_size_stats["inspect.json"] = {0, 0};
    file_size_stats["other"] = {0, zip_file_bytes};

    inspect_data_budget_->UpdateBudget(file_size_stats);
  }

  void CalcBudget(const std::map<std::string, ArchiveFileStats>& file_size_stats) {
    inspect_data_budget_->UpdateBudget(file_size_stats);
  }

  std::optional<size_t> GetSizeInBytes() { return inspect_data_budget_->SizeInBytes(); }

 private:
  std::unique_ptr<InspectNodeManager> inspect_node_manager_;

  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<InspectDataBudget> inspect_data_budget_;

  std::unique_ptr<cobalt::Logger> cobalt_;
};

TEST_F(InspectDataBudgetTest, TestUnlimitedBudget) {
  MakeUnlimitedBudget();
  ASSERT_FALSE(GetSizeInBytes());

  // setting a budget should not do anything.
  CalcBudget(1 * kMegabytes);
  ASSERT_FALSE(GetSizeInBytes());
}

TEST_F(InspectDataBudgetTest, TestLimitedBudget) {
  MakeLimitedBudget();
  ASSERT_TRUE(GetSizeInBytes());
}

TEST_F(InspectDataBudgetTest, TestForCrash_MissingSizeStats) {
  MakeLimitedBudget();
  std::map<std::string, ArchiveFileStats> file_size_stats;
  CalcBudget(file_size_stats);
}

TEST_F(InspectDataBudgetTest, TestSizeBudget_Maintain) {
  MakeLimitedBudget();
  ASSERT_TRUE(GetSizeInBytes());
  size_t initial_budget = GetSizeInBytes().value();

  CalcBudget(2 * kMegabytes);
  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), initial_budget);
}

TEST_F(InspectDataBudgetTest, TestSizeBudget_UpperLimit) {
  MakeLimitedBudget();
  ASSERT_TRUE(GetSizeInBytes());
  size_t initial_budget = GetSizeInBytes().value();
  CalcBudget(724 * kKilobytes);

  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), initial_budget);
}

TEST_F(InspectDataBudgetTest, TestSizeBudget_LowerLimit) {
  // Arrive at the lower limit by making the zip size 2 GB twice (this should reduce the initial
  // budget at most by 2^16 times).
  MakeLimitedBudget();
  CalcBudget(2 * kGigabytes);
  CalcBudget(2 * kGigabytes);
  ASSERT_TRUE(GetSizeInBytes());
  size_t lower_limit = GetSizeInBytes().value();

  CalcBudget(kGigabytes);
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

  CalcBudget(3000 * kKilobytes);
  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), budget);

  // Note: Make sure that the geometric mean of the last zip size and the new zip size > 2MB.
  // Otherwise the resulting budget might be lower than our calculated value due to upper limit
  // restrictions.
  budget = (budget * 1024) / 800;
  CalcBudget(1600 * kKilobytes);
  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), budget);
}

TEST_F(InspectDataBudgetTest, TestInspectBudget_BudgetDisabled) {
  MakeUnlimitedBudget();

  const auto node = AllOf(
      NodeMatches(AllOf(NameMatches("inspect_budget"), PropertyList(UnorderedElementsAreArray({
                                                           StringIs("is_budget_enabled", "false"),
                                                       })))),
      ChildrenMatch(IsEmpty()));

  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAre(AllOf(node))));
}

TEST_F(InspectDataBudgetTest, TestInspectBudget_BudgetEnabled) {
  MakeLimitedBudget();
  ASSERT_TRUE(GetSizeInBytes());
  size_t initial_budget = GetSizeInBytes().value();

  CalcBudget(1 * kMegabytes);
  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), initial_budget);

  const auto budget =
      AllOf(NodeMatches(AllOf(NameMatches("last_ten_input_budget_previous_snapshot_size_bytes"),
                              PropertyList(UnorderedElementsAreArray({
                                  UintArrayIs("0", std::vector<uint64_t>{20971520u, 1048576u}),
                              })))),
            ChildrenMatch(IsEmpty()));

  const auto node = AllOf(NodeMatches(AllOf(NameMatches("inspect_budget"),
                                            PropertyList(UnorderedElementsAreArray({
                                                UintIs("min_input_budget_bytes", 4194304u),
                                                UintIs("max_input_budget_bytes", 20971520u),
                                                UintIs("target_snapshot_size_bytes", 2097152u),
                                                StringIs("is_budget_enabled", "true"),
                                            })))),
                          ChildrenMatch(UnorderedElementsAre(budget)));

  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAre(AllOf(node))));
}

TEST_F(InspectDataBudgetTest, TestInspectBudget_MaxEntries) {
  MakeLimitedBudget();

  // zip_size = min(floor(budget * 0.2), 3MB) + 512KB
  for (int i = 0; i < 12; i++) {
    const size_t budget = GetSizeInBytes().value();
    const float compression_ratio = 0.2;
    const size_t zip_size =
        std::min<size_t>(std::floor(budget * compression_ratio), 3 * kMegabytes) + 512 * kKilobytes;
    CalcBudget(zip_size);
  }

  const auto budget =
      AllOf(NodeMatches(AllOf(NameMatches("last_ten_input_budget_previous_snapshot_size_bytes"),
                              PropertyList(UnorderedElementsAreArray({
                                  UintArrayIs("2", std::vector<uint64_t>{8036989u, 2245028u}),
                                  UintArrayIs("3", std::vector<uint64_t>{7906790u, 2131685u}),
                                  UintArrayIs("4", std::vector<uint64_t>{7874894u, 2105646u}),
                                  UintArrayIs("5", std::vector<uint64_t>{7866963u, 2099266u}),
                                  UintArrayIs("6", std::vector<uint64_t>{7864982u, 2097680u}),
                                  UintArrayIs("7", std::vector<uint64_t>{7864486u, 2097284u}),
                                  UintArrayIs("8", std::vector<uint64_t>{7864362u, 2097185u}),
                                  UintArrayIs("9", std::vector<uint64_t>{7864331u, 2097160u}),
                                  UintArrayIs("10", std::vector<uint64_t>{7864323u, 2097154u}),
                                  UintArrayIs("11", std::vector<uint64_t>{7864323u, 2097152u}),
                              })))),
            ChildrenMatch(IsEmpty()));

  const auto node = AllOf(NodeMatches(AllOf(NameMatches("inspect_budget"),
                                            PropertyList(UnorderedElementsAreArray({
                                                UintIs("min_input_budget_bytes", 4194304u),
                                                UintIs("max_input_budget_bytes", 20971520u),
                                                UintIs("target_snapshot_size_bytes", 2097152u),
                                                StringIs("is_budget_enabled", "true"),
                                            })))),
                          ChildrenMatch(UnorderedElementsAre(budget)));

  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAre(AllOf(node))));
}

TEST_F(InspectDataBudgetTest, TestCobalt_BudgetEnabled) {
  MakeLimitedBudget();
  ASSERT_TRUE(GetSizeInBytes());
  size_t initial_budget = GetSizeInBytes().value();

  CalcBudget(1 * kMegabytes);
  ASSERT_TRUE(GetSizeInBytes());
  ASSERT_EQ(GetSizeInBytes().value(), initial_budget);

  RunLoopUntilIdle();

  EXPECT_THAT(
      ReceivedCobaltEvents(),
      UnorderedElementsAreArray({
          cobalt::Event(cobalt::EventType::kInteger, cobalt::kInspectBudgetMetricId, {}, 20971520),
      }));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
