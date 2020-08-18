// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/thermal_agent.h"

#include <fuchsia/thermal/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>

#include "src/media/audio/audio_core/process_config.h"

namespace media::audio {
namespace {

using fuchsia::thermal::TripPoint;

bool TripPointsEqual(const std::vector<TripPoint>& a, const std::vector<TripPoint>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i].deactivate_below != b[i].deactivate_below || a[i].activate_at != b[i].activate_at) {
      return false;
    }
  }
  return true;
}

std::vector<ThermalConfig::StateTransition> MakeTransitions(
    std::vector<std::pair<const char*, const char*>> args) {
  std::vector<ThermalConfig::StateTransition> transitions;
  transitions.reserve(args.size());
  for (const auto& [target_name, transition] : args) {
    transitions.emplace_back(target_name, transition);
  }
  return transitions;
}

class FakeThermalController : public fuchsia::thermal::Controller {
 public:
  FakeThermalController() : binding_(this) {}

  void Bind(fidl::InterfaceRequest<fuchsia::thermal::Controller> request) {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([this](zx_status_t status) { binding_error_ = status; });
  }

  void ExpectSubscribeNotCalled() const { EXPECT_FALSE(actor_.is_bound()); }

  void ExpectSubscribeCalled(fuchsia::thermal::ActorType actor_type,
                             std::vector<TripPoint> trip_points) const {
    EXPECT_TRUE(actor_.is_bound());
    EXPECT_EQ(actor_type, actor_type_);
    EXPECT_TRUE(TripPointsEqual(trip_points, trip_points_));
  }

  zx_status_t binding_error() const { return binding_error_; }

  void SetThermalState(uint32_t state) {
    EXPECT_TRUE(actor_.is_bound());
    actor_->SetThermalState(state, [this]() { set_thermal_state_returned_ = true; });
  }

  bool& set_thermal_state_returned() { return set_thermal_state_returned_; }

  // fuchsia::thermal::Controller implementation.
  void Subscribe(fidl::InterfaceHandle<class fuchsia::thermal::Actor> actor,
                 fuchsia::thermal::ActorType actor_type, std::vector<TripPoint> trip_points,
                 SubscribeCallback callback) override {
    actor_.Bind(std::move(actor));
    actor_type_ = actor_type;
    trip_points_.assign(trip_points.begin(), trip_points.end());
    callback(fuchsia::thermal::Controller_Subscribe_Result::WithResponse(
        fuchsia::thermal::Controller_Subscribe_Response()));
  }

 private:
  fidl::Binding<fuchsia::thermal::Controller> binding_;
  fuchsia::thermal::ActorPtr actor_;
  fuchsia::thermal::ActorType actor_type_;
  std::vector<TripPoint> trip_points_;
  zx_status_t binding_error_ = ZX_OK;
  bool set_thermal_state_returned_ = false;
};

class ThermalAgentTest : public gtest::TestLoopFixture {
 public:
  ThermalAgent::SetConfigCallback ConfigCallback() {
    return fit::bind_member(this, &ThermalAgentTest::HandleConfigAction);
  }

  void ExpectConfigAction(const std::string& target_name, const std::string& config) {
    for (auto iter = config_actions_.begin(); iter != config_actions_.end(); ++iter) {
      if (iter->target_name_ == target_name && iter->config_ == config) {
        config_actions_.erase(iter);
        return;
      }
    }

    EXPECT_TRUE(false) << "Didn't find expected config action for target " << target_name
                       << ", value " << config;
  }

  void ExpectNoOtherConfigAction() {
    EXPECT_TRUE(config_actions_.empty());
    config_actions_.clear();
  }

 private:
  struct ConfigAction {
    std::string target_name_;
    std::string config_;
  };

  void HandleConfigAction(const std::string& target_name, const std::string& config) {
    config_actions_.push_back({target_name, config});
  }

  std::vector<ConfigAction> config_actions_;
};

// Verifies that the thermal agent does nothing if the config contains no thermal config entries.
TEST_F(ThermalAgentTest, NoConfigEntries) {
  ProcessConfig process_config = ProcessConfigBuilder()
                                     .SetDefaultVolumeCurve(VolumeCurve::DefaultForMinGain(
                                         VolumeCurve::kDefaultGainForMinVolume))
                                     .Build();
  fuchsia::thermal::ControllerPtr thermal_controller;
  FakeThermalController fake_thermal_controller;
  fake_thermal_controller.Bind(thermal_controller.NewRequest());

  ThermalAgent under_test(std::move(thermal_controller), process_config.thermal_config(),
                          process_config.device_config(), ConfigCallback());

  RunLoopUntilIdle();
  EXPECT_NE(ZX_OK, fake_thermal_controller.binding_error());
  ExpectNoOtherConfigAction();
  fake_thermal_controller.ExpectSubscribeNotCalled();
}

const char kTargetName[] = "target_name";
const char kNominalConfig[] = "nominal_config";
constexpr TripPoint kTripPoint{47, 53};
const char kThrottledConfig[] = "config";

// Verifies that the thermal agent works properly with a single thermal config entry with one
// trip point.
TEST_F(ThermalAgentTest, OneConfigEntry) {
  PipelineConfig pipeline_config({"mixgroup",
                                  {},
                                  {{"lib", "effect", kTargetName, kNominalConfig, std::nullopt}},
                                  {},
                                  false,
                                  48000,
                                  2});
  auto transitions = MakeTransitions({{kTargetName, {kThrottledConfig}}});
  ProcessConfig process_config =
      ProcessConfigBuilder()
          .SetDefaultVolumeCurve(
              VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume))
          .AddDeviceProfile({std::nullopt, DeviceConfig::OutputDeviceProfile(
                                               false, {}, false, std::move(pipeline_config))})
          .AddThermalPolicyEntry(ThermalConfig::Entry(kTripPoint, transitions))
          .Build();
  fuchsia::thermal::ControllerPtr thermal_controller;
  FakeThermalController fake_thermal_controller;
  fake_thermal_controller.Bind(thermal_controller.NewRequest());

  ThermalAgent under_test(std::move(thermal_controller), process_config.thermal_config(),
                          process_config.device_config(), ConfigCallback());

  RunLoopUntilIdle();
  EXPECT_NE(ZX_OK, fake_thermal_controller.binding_error());
  ExpectNoOtherConfigAction();
  fake_thermal_controller.ExpectSubscribeCalled(fuchsia::thermal::ActorType::AUDIO, {kTripPoint});

  // Thermal config in effect.
  fake_thermal_controller.SetThermalState(1);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName, kThrottledConfig);
  ExpectNoOtherConfigAction();
  fake_thermal_controller.set_thermal_state_returned() = false;

  // Nominal config restored.
  fake_thermal_controller.SetThermalState(0);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName, kNominalConfig);
  ExpectNoOtherConfigAction();
}

const char kTargetName0[] = "target_name_0";
const char kTargetName1[] = "target_name_1";
const char kTargetName2[] = "target_name_2";
const char kNominalConfig0[] = "nominal_config_0";
const char kNominalConfig1[] = "nominal_config_1";
const char kNominalConfig2[] = "nominal_config_2";
constexpr TripPoint kTripPoint0{59, 61};
constexpr TripPoint kTripPoint1{69, 71};
constexpr TripPoint kTripPoint2{79, 81};
const char kTripPoint0Config0[] = "config0_0";
const char kTripPoint0Config1[] = "config0_1";
const char kTripPoint1Config1[] = "config1_1";
const char kTripPoint1Config2[] = "config1_2";
const char kTripPoint2Config0[] = "config2_0";
const char kTripPoint2Config2[] = "config2_2";

// Verifies that the thermal agent works properly with multiple thermal config entries, each with
// multiple trip points.
//
// We specify three trip points, and thus four thermal states. The configs for each target can be
// portrayed relative to the thermal state and trip points as follows:
//
// Thermal state | target_name_0    | target_name_1    | target_name_2    |
// --------------+------------------+------------------+------------------+
//             0 | nominal_config_0 | nominal_config_1 | nominal_config_2 |
// --------------+------------------+------------------+------------------+ <-- kTripPoint0
//             1 | config0_0        | config0_1        | nominal_config_2 |
// --------------+------------------+------------------+------------------+ <-- kTripPoint1
//             2 | config0_0        | config1_1        | config1_2        |
// --------------+------------------+------------------+------------------+ <-- kTripPoint2
//             3 | config2_0        | config1_1        | config2_2        |
// --------------+------------------+------------------+------------------+
//
// Note that whenever a given target is not included in a transition, its config does not change
// across the corresponding trip point.
TEST_F(ThermalAgentTest, MultipleConfigEntries) {
  PipelineConfig pipeline_config({"mixgroup",
                                  {},
                                  {{"lib", "effect", kTargetName0, kNominalConfig0, std::nullopt},
                                   {"lib", "effect", kTargetName1, kNominalConfig1, std::nullopt},
                                   {"lib", "effect", kTargetName2, kNominalConfig2, std::nullopt}},
                                  {},
                                  false,
                                  48000,
                                  2});
  auto transitions0 =
      MakeTransitions({{kTargetName0, kTripPoint0Config0}, {kTargetName1, kTripPoint0Config1}});
  auto transitions1 =
      MakeTransitions({{kTargetName1, kTripPoint1Config1}, {kTargetName2, kTripPoint1Config2}});
  auto transitions2 =
      MakeTransitions({{kTargetName0, kTripPoint2Config0}, {kTargetName2, kTripPoint2Config2}});
  ProcessConfig process_config =
      ProcessConfigBuilder()
          .SetDefaultVolumeCurve(
              VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume))
          .AddDeviceProfile({std::nullopt, DeviceConfig::OutputDeviceProfile(
                                               false, {}, false, std::move(pipeline_config))})
          .AddThermalPolicyEntry(ThermalConfig::Entry(kTripPoint0, transitions0))
          .AddThermalPolicyEntry(ThermalConfig::Entry(kTripPoint1, transitions1))
          .AddThermalPolicyEntry(ThermalConfig::Entry(kTripPoint2, transitions2))
          .Build();
  fuchsia::thermal::ControllerPtr thermal_controller;
  FakeThermalController fake_thermal_controller;
  fake_thermal_controller.Bind(thermal_controller.NewRequest());

  ThermalAgent under_test(std::move(thermal_controller), process_config.thermal_config(),
                          process_config.device_config(), ConfigCallback());

  RunLoopUntilIdle();
  EXPECT_NE(ZX_OK, fake_thermal_controller.binding_error());
  ExpectNoOtherConfigAction();
  // We expect three trip points (4 states).
  fake_thermal_controller.ExpectSubscribeCalled(fuchsia::thermal::ActorType::AUDIO,
                                                {kTripPoint0, kTripPoint1, kTripPoint2});

  // Thermal config for state 1 in effect.
  fake_thermal_controller.SetThermalState(1);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName0, kTripPoint0Config0);
  ExpectConfigAction(kTargetName1, kTripPoint0Config1);
  ExpectNoOtherConfigAction();
  fake_thermal_controller.set_thermal_state_returned() = false;

  // Thermal config for state 2 in effect.
  fake_thermal_controller.SetThermalState(2);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName1, kTripPoint1Config1);
  ExpectConfigAction(kTargetName2, kTripPoint1Config2);
  ExpectNoOtherConfigAction();
  fake_thermal_controller.set_thermal_state_returned() = false;

  // Thermal config for state 3 in effect.
  fake_thermal_controller.SetThermalState(3);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName0, kTripPoint2Config0);
  ExpectConfigAction(kTargetName2, kTripPoint2Config2);
  ExpectNoOtherConfigAction();
  fake_thermal_controller.set_thermal_state_returned() = false;

  // Thermal config for state 1 in effect.
  fake_thermal_controller.SetThermalState(1);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName0, kTripPoint0Config0);
  ExpectConfigAction(kTargetName1, kTripPoint0Config1);
  ExpectConfigAction(kTargetName2, kNominalConfig2);
  ExpectNoOtherConfigAction();
  fake_thermal_controller.set_thermal_state_returned() = false;

  // Nominal config restored.
  fake_thermal_controller.SetThermalState(0);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName0, kNominalConfig0);
  ExpectConfigAction(kTargetName1, kNominalConfig1);
  // kTargetName2 is already set to kNominalConfig2
  ExpectNoOtherConfigAction();
}

}  // namespace
}  // namespace media::audio
