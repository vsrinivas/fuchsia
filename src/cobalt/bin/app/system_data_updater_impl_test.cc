// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/system_data_updater_impl.h"

#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include <fstream>

#include "fuchsia/cobalt/cpp/fidl.h"

namespace cobalt {

using encoder::SystemData;
using fidl::VectorPtr;
using fuchsia::cobalt::ExperimentPtr;
using fuchsia::cobalt::SoftwareDistributionInfo;
using fuchsia::cobalt::Status;
using fuchsia::cobalt::SystemDataUpdaterPtr;
using inspect::testing::ChildrenMatch;
using inspect::testing::IntIs;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using ::testing::UnorderedElementsAre;

class CobaltAppForTest {
 public:
  CobaltAppForTest(std::unique_ptr<sys::ComponentContext> context)
      : system_data_("test", "test", ReleaseStage::DEBUG), context_(std::move(context)) {
    system_data_updater_impl_.reset(new SystemDataUpdaterImpl(
        inspector_.GetRoot().CreateChild("system_data"), &system_data_, "/tmp/test_"));

    context_->outgoing()->AddPublicService(
        system_data_updater_bindings_.GetHandler(system_data_updater_impl_.get()));
  }

  void ClearData() { system_data_updater_impl_->ClearData(); }

  const SystemData& system_make_data() { return system_data_; }

  const inspect::Inspector& inspector() { return inspector_; }

 private:
  inspect::Inspector inspector_;
  SystemData system_data_;

  std::unique_ptr<sys::ComponentContext> context_;

  std::unique_ptr<SystemDataUpdaterImpl> system_data_updater_impl_;
  fidl::BindingSet<fuchsia::cobalt::SystemDataUpdater> system_data_updater_bindings_;
};

class SystemDataUpdaterImplTests : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    cobalt_app_.reset(new CobaltAppForTest(context_provider_.TakeContext()));
  }

  void TearDown() override {
    cobalt_app_->ClearData();
    cobalt_app_.reset();
    TestLoopFixture::TearDown();
  }

 protected:
  SystemDataUpdaterPtr GetSystemDataUpdater() {
    SystemDataUpdaterPtr system_data_updater;
    context_provider_.ConnectToPublicService(system_data_updater.NewRequest());
    return system_data_updater;
  }

  const std::vector<Experiment>& experiments() {
    return cobalt_app_->system_make_data().experiments();
  }

  const std::string& channel() {
    return cobalt_app_->system_make_data().system_profile().channel();
  }

  const std::string& realm() { return cobalt_app_->system_make_data().system_profile().realm(); }

  std::vector<fuchsia::cobalt::Experiment> ExperimentVectorWithIdAndArmId(int64_t experiment_id,
                                                                          int64_t arm_id) {
    std::vector<fuchsia::cobalt::Experiment> vector;

    fuchsia::cobalt::Experiment experiment;
    experiment.experiment_id = experiment_id;
    experiment.arm_id = arm_id;
    vector.push_back(experiment);
    return vector;
  }

  inspect::Hierarchy InspectHierarchy() {
    fpromise::result<inspect::Hierarchy> result =
        inspect::ReadFromVmo(cobalt_app_->inspector().DuplicateVmo());
    return result.take_value();
  }

 private:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<CobaltAppForTest> cobalt_app_;
};

TEST_F(SystemDataUpdaterImplTests, SetExperimentStateFromNull) {
  int64_t kExperimentId = 1;
  int64_t kArmId = 123;
  SystemDataUpdaterPtr system_data_updater = GetSystemDataUpdater();

  EXPECT_TRUE(experiments().empty());

  system_data_updater->SetExperimentState(ExperimentVectorWithIdAndArmId(kExperimentId, kArmId),
                                          [&](Status s) {});

  RunLoopUntilIdle();

  EXPECT_FALSE(experiments().empty());
  EXPECT_EQ(experiments().front().experiment_id(), kExperimentId);
  EXPECT_EQ(experiments().front().arm_id(), kArmId);
}

TEST_F(SystemDataUpdaterImplTests, UpdateExperimentState) {
  int64_t kInitialExperimentId = 1;
  int64_t kInitialArmId = 123;
  int64_t kUpdatedExperimentId = 1;
  int64_t kUpdatedArmId = 123;
  SystemDataUpdaterPtr system_data_updater = GetSystemDataUpdater();

  system_data_updater->SetExperimentState(
      ExperimentVectorWithIdAndArmId(kInitialExperimentId, kInitialArmId), [&](Status s) {});
  RunLoopUntilIdle();

  EXPECT_FALSE(experiments().empty());
  EXPECT_EQ(experiments().front().experiment_id(), kInitialExperimentId);
  EXPECT_EQ(experiments().front().arm_id(), kInitialArmId);

  system_data_updater->SetExperimentState(
      ExperimentVectorWithIdAndArmId(kUpdatedExperimentId, kUpdatedArmId), [&](Status s) {});
  RunLoopUntilIdle();

  EXPECT_FALSE(experiments().empty());
  EXPECT_EQ(experiments().front().experiment_id(), kUpdatedExperimentId);
  EXPECT_EQ(experiments().front().arm_id(), kUpdatedArmId);
}

TEST_F(SystemDataUpdaterImplTests, SetSoftwareDistributionInfo) {
  SystemDataUpdaterPtr system_data_updater = GetSystemDataUpdater();

  EXPECT_EQ(channel(), "<unset>");
  EXPECT_EQ(realm(), "<unset>");
  EXPECT_THAT(InspectHierarchy(),
              AllOf(NodeMatches(NameMatches("root")),
                    ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                        NameMatches("system_data"),
                        PropertyList(UnorderedElementsAre(IntIs("fidl_calls", 0),
                                                          StringIs("channel", "<unset>"),
                                                          StringIs("realm", "<unset>")))))))));

  SoftwareDistributionInfo info = SoftwareDistributionInfo();
  info.set_current_realm("");
  system_data_updater->SetSoftwareDistributionInfo(std::move(info), [](Status s) {});
  RunLoopUntilIdle();

  EXPECT_EQ(channel(), "<unset>");
  EXPECT_EQ(realm(), "<unknown>");
  EXPECT_THAT(InspectHierarchy(),
              AllOf(NodeMatches(NameMatches("root")),
                    ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                        NameMatches("system_data"),
                        PropertyList(UnorderedElementsAre(IntIs("fidl_calls", 1),
                                                          StringIs("channel", "<unset>"),
                                                          StringIs("realm", "<unknown>")))))))));

  info = SoftwareDistributionInfo();
  info.set_current_realm("dogfood");
  info.set_current_channel("fishfood_release");
  system_data_updater->SetSoftwareDistributionInfo(std::move(info), [](Status s) {});
  RunLoopUntilIdle();

  EXPECT_EQ(channel(), "fishfood_release");
  EXPECT_EQ(realm(), "dogfood");
  EXPECT_THAT(InspectHierarchy(),
              AllOf(NodeMatches(NameMatches("root")),
                    ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                        NameMatches("system_data"),
                        PropertyList(UnorderedElementsAre(IntIs("fidl_calls", 2),
                                                          StringIs("channel", "fishfood_release"),
                                                          StringIs("realm", "dogfood")))))))));

  // Set one software distribution field without overriding the other.
  info = SoftwareDistributionInfo();
  info.set_current_channel("test_channel");
  system_data_updater->SetSoftwareDistributionInfo(std::move(info), [](Status s) {});
  RunLoopUntilIdle();

  EXPECT_EQ(channel(), "test_channel");
  EXPECT_EQ(realm(), "dogfood");
  EXPECT_THAT(InspectHierarchy(),
              AllOf(NodeMatches(NameMatches("root")),
                    ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                        NameMatches("system_data"),
                        PropertyList(UnorderedElementsAre(IntIs("fidl_calls", 3),
                                                          StringIs("channel", "test_channel"),
                                                          StringIs("realm", "dogfood")))))))));
}

namespace {

std::unique_ptr<SystemData> make_data() {
  return std::make_unique<SystemData>("test", "test", ReleaseStage::DEBUG);
}

std::unique_ptr<SystemDataUpdaterImpl> make_updater(inspect::Node node, SystemData* data) {
  return std::make_unique<SystemDataUpdaterImpl>(std::move(node), data, "/tmp/test_");
}

}  // namespace

TEST(SystemDataUpdaterImpl, TestSoftwareDistributionInfoPersistence) {
  inspect::Inspector inspector;
  auto system_data = make_data();
  auto updater = make_updater(inspector.GetRoot().CreateChild("system_data"), system_data.get());

  EXPECT_EQ(system_data->system_profile().channel(), "<unset>");
  EXPECT_EQ(system_data->system_profile().realm(), "<unset>");
  EXPECT_THAT(inspect::ReadFromVmo(inspector.DuplicateVmo()).take_value(),
              AllOf(NodeMatches(NameMatches("root")),
                    ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                        NameMatches("system_data"),
                        PropertyList(UnorderedElementsAre(IntIs("fidl_calls", 0),
                                                          StringIs("channel", "<unset>"),
                                                          StringIs("realm", "<unset>")))))))));

  SoftwareDistributionInfo info = SoftwareDistributionInfo();
  info.set_current_realm("dogfood");
  info.set_current_channel("fishfood_release");
  updater->SetSoftwareDistributionInfo(std::move(info), [](Status s) {});
  EXPECT_EQ(system_data->system_profile().realm(), "dogfood");
  EXPECT_EQ(system_data->system_profile().channel(), "fishfood_release");
  EXPECT_THAT(inspect::ReadFromVmo(inspector.DuplicateVmo()).take_value(),
              AllOf(NodeMatches(NameMatches("root")),
                    ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                        NameMatches("system_data"),
                        PropertyList(UnorderedElementsAre(IntIs("fidl_calls", 1),
                                                          StringIs("channel", "fishfood_release"),
                                                          StringIs("realm", "dogfood")))))))));

  // Test restoring data.
  inspector = inspect::Inspector();
  system_data = make_data();
  updater = make_updater(inspector.GetRoot().CreateChild("system_data"), system_data.get());
  EXPECT_EQ(system_data->system_profile().realm(), "dogfood");
  EXPECT_EQ(system_data->system_profile().channel(), "fishfood_release");
  EXPECT_THAT(inspect::ReadFromVmo(inspector.DuplicateVmo()).take_value(),
              AllOf(NodeMatches(NameMatches("root")),
                    ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                        NameMatches("system_data"),
                        PropertyList(UnorderedElementsAre(IntIs("fidl_calls", 0),
                                                          StringIs("channel", "fishfood_release"),
                                                          StringIs("realm", "dogfood")))))))));

  // Test default behavior with no data.
  updater->ClearData();
  inspector = inspect::Inspector();
  system_data = make_data();
  updater = make_updater(inspector.GetRoot().CreateChild("system_data"), system_data.get());
  EXPECT_EQ(system_data->system_profile().channel(), "<unset>");
  EXPECT_EQ(system_data->system_profile().realm(), "<unset>");
  EXPECT_THAT(inspect::ReadFromVmo(inspector.DuplicateVmo()).take_value(),
              AllOf(NodeMatches(NameMatches("root")),
                    ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                        NameMatches("system_data"),
                        PropertyList(UnorderedElementsAre(IntIs("fidl_calls", 0),
                                                          StringIs("channel", "<unset>"),
                                                          StringIs("realm", "<unset>")))))))));
}

}  // namespace cobalt
