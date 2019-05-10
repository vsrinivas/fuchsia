// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/system_data_updater_impl.h"

#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

namespace cobalt {

using encoder::SystemData;
using fidl::VectorPtr;
using fuchsia::cobalt::ExperimentPtr;
using fuchsia::cobalt::Status;
using fuchsia::cobalt::SystemDataUpdaterPtr;

class CobaltAppForTest {
 public:
  CobaltAppForTest(std::unique_ptr<sys::ComponentContext> context)
      : system_data_("test", "test"), context_(std::move(context)) {
    system_data_updater_impl_.reset(new SystemDataUpdaterImpl(&system_data_));

    context_->outgoing()->AddPublicService(
        system_data_updater_bindings_.GetHandler(
            system_data_updater_impl_.get()));
  }

  const SystemData& system_data() { return system_data_; }

 private:
  SystemData system_data_;

  std::unique_ptr<sys::ComponentContext> context_;

  std::unique_ptr<fuchsia::cobalt::SystemDataUpdater> system_data_updater_impl_;
  fidl::BindingSet<fuchsia::cobalt::SystemDataUpdater>
      system_data_updater_bindings_;
};

class SystemDataUpdaterImplTests : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    cobalt_app_.reset(new CobaltAppForTest(context_provider_.TakeContext()));
  }

  void TearDown() override {
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
    return cobalt_app_->system_data().experiments();
  }

  const std::string& channel() {
    return cobalt_app_->system_data().system_profile().channel();
  }

  VectorPtr<fuchsia::cobalt::Experiment> ExperimentVectorWithIdAndArmId(
      int64_t experiment_id, int64_t arm_id) {
    VectorPtr<fuchsia::cobalt::Experiment> vector;

    fuchsia::cobalt::Experiment experiment;
    experiment.experiment_id = experiment_id;
    experiment.arm_id = arm_id;
    vector.push_back(experiment);
    return vector;
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

  system_data_updater->SetExperimentState(
      ExperimentVectorWithIdAndArmId(kExperimentId, kArmId), [&](Status s) {});

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
      ExperimentVectorWithIdAndArmId(kInitialExperimentId, kInitialArmId),
      [&](Status s) {});
  RunLoopUntilIdle();

  EXPECT_FALSE(experiments().empty());
  EXPECT_EQ(experiments().front().experiment_id(), kInitialExperimentId);
  EXPECT_EQ(experiments().front().arm_id(), kInitialArmId);

  system_data_updater->SetExperimentState(
      ExperimentVectorWithIdAndArmId(kUpdatedExperimentId, kUpdatedArmId),
      [&](Status s) {});
  RunLoopUntilIdle();

  EXPECT_FALSE(experiments().empty());
  EXPECT_EQ(experiments().front().experiment_id(), kUpdatedExperimentId);
  EXPECT_EQ(experiments().front().arm_id(), kUpdatedArmId);
}

TEST_F(SystemDataUpdaterImplTests, SetChannel) {
  SystemDataUpdaterPtr system_data_updater = GetSystemDataUpdater();

  EXPECT_EQ(channel(), "<unset>");

  system_data_updater->SetChannel("", [](Status s) {});
  RunLoopUntilIdle();

  EXPECT_EQ(channel(), "<unknown>");

  system_data_updater->SetChannel("fishfood", [](Status s) {});
  RunLoopUntilIdle();

  EXPECT_EQ(channel(), "fishfood");
}

}  // namespace cobalt
