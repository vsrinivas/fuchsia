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

class FakeThermalController : public fuchsia::thermal::Controller {
 public:
  FakeThermalController() : binding_(this) {}

  void Bind(fidl::InterfaceRequest<fuchsia::thermal::Controller> request) {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([this](zx_status_t status) { binding_error_ = status; });
  }

  void ExpectSubscribeNotCalled() const { EXPECT_FALSE(actor_.is_bound()); }

  void ExpectSubscribeCalled(fuchsia::thermal::ActorType actor_type,
                             std::vector<uint32_t> trip_points) const {
    EXPECT_TRUE(actor_.is_bound());
    EXPECT_EQ(actor_type, actor_type_);
    EXPECT_EQ(trip_points, trip_points_);
  }

  zx_status_t binding_error() const { return binding_error_; }

  void SetThermalState(uint32_t state) {
    EXPECT_TRUE(actor_.is_bound());
    actor_->SetThermalState(state, [this]() { set_thermal_state_returned_ = true; });
  }

  bool& set_thermal_state_returned() { return set_thermal_state_returned_; }

  // fuchsia::thermal::Controller implementation.
  void Subscribe(fidl::InterfaceHandle<class fuchsia::thermal::Actor> actor,
                 fuchsia::thermal::ActorType actor_type, std::vector<uint32_t> trip_points,
                 SubscribeCallback callback) override {
    FX_LOGS(INFO) << "Subscribe: " << (actor ? "actor" : "no actor");
    actor_.Bind(std::move(actor));
    actor_type_ = actor_type;
    trip_points_ = std::move(trip_points);
    callback(fuchsia::thermal::Controller_Subscribe_Result::WithResponse(
        fuchsia::thermal::Controller_Subscribe_Response()));
  }

 private:
  fidl::Binding<fuchsia::thermal::Controller> binding_;
  fuchsia::thermal::ActorPtr actor_;
  fuchsia::thermal::ActorType actor_type_;
  std::vector<uint32_t> trip_points_;
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

// Verifies that the thermal agent does nothing if the config contains to thermal config entries.
TEST_F(ThermalAgentTest, NoConfigEntries) {
  ProcessConfig process_config = ProcessConfigBuilder()
                                     .SetDefaultVolumeCurve(VolumeCurve::DefaultForMinGain(
                                         VolumeCurve::kDefaultGainForMinVolume))
                                     .Build();
  fuchsia::thermal::ControllerPtr thermal_controller;
  FakeThermalController fake_thermal_controller;
  fake_thermal_controller.Bind(thermal_controller.NewRequest());

  ThermalAgent under_test(std::move(thermal_controller), process_config.thermal_config(),
                          process_config.routing_config(), ConfigCallback());

  RunLoopUntilIdle();
  EXPECT_NE(ZX_OK, fake_thermal_controller.binding_error());
  ExpectNoOtherConfigAction();
  fake_thermal_controller.ExpectSubscribeNotCalled();
}

const char kTargetName[] = "target_name";
const char kNominalConfig[] = "nominal_config";
constexpr uint32_t kTripPoint = 50;
const char kConfigTripPoint[] = "config";

// Verifies that the thermal agent works properly with a single thermal config entry with one trip
// point.
TEST_F(ThermalAgentTest, OneConfigEntry) {
  PipelineConfig pipeline_config(
      {"mixgroup", {}, {{"lib", "effect", kTargetName, kNominalConfig}}, {}, false});
  std::vector<ThermalConfig::State> states{{kTripPoint, kConfigTripPoint}};
  ProcessConfig process_config =
      ProcessConfigBuilder()
          .SetDefaultVolumeCurve(
              VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume))
          .AddDeviceRoutingProfile(
              {std::nullopt,
               RoutingConfig::DeviceProfile(false, {}, false, std::move(pipeline_config))})
          .AddThermalPolicyEntry(ThermalConfig::Entry(kTargetName, states))
          .Build();
  fuchsia::thermal::ControllerPtr thermal_controller;
  FakeThermalController fake_thermal_controller;
  fake_thermal_controller.Bind(thermal_controller.NewRequest());

  ThermalAgent under_test(std::move(thermal_controller), process_config.thermal_config(),
                          process_config.routing_config(), ConfigCallback());

  RunLoopUntilIdle();
  EXPECT_NE(ZX_OK, fake_thermal_controller.binding_error());
  ExpectNoOtherConfigAction();
  fake_thermal_controller.ExpectSubscribeCalled(fuchsia::thermal::ActorType::AUDIO, {kTripPoint});

  // Thermal config in effect.
  fake_thermal_controller.SetThermalState(1);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName, kConfigTripPoint);
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
constexpr uint32_t kTripPoint0_0 = 60;
constexpr uint32_t kTripPoint0_1 = 80;
constexpr uint32_t kTripPoint1_0 = 70;
constexpr uint32_t kTripPoint1_1 = 80;
constexpr uint32_t kTripPoint2_0 = 70;
constexpr uint32_t kTripPoint2_1 = 90;
const char kConfigTripPoint0_0[] = "config0_0";
const char kConfigTripPoint0_1[] = "config0_1";
const char kConfigTripPoint1_0[] = "config1_0";
const char kConfigTripPoint1_1[] = "config1_1";
const char kConfigTripPoint2_0[] = "config2_0";
const char kConfigTripPoint2_1[] = "config2_1";

// Verifies that the thermal agent works properly with multiple thermal config entries, each with
// multiple trip points.
TEST_F(ThermalAgentTest, MultipleConfigEntries) {
  PipelineConfig pipeline_config({"mixgroup",
                                  {},
                                  {{"lib", "effect", kTargetName0, kNominalConfig0},
                                   {"lib", "effect", kTargetName1, kNominalConfig1},
                                   {"lib", "effect", kTargetName2, kNominalConfig2}},
                                  {},
                                  false});
  std::vector<ThermalConfig::State> states0{{kTripPoint0_0, kConfigTripPoint0_0},
                                            {kTripPoint0_1, kConfigTripPoint0_1}};
  std::vector<ThermalConfig::State> states1{{kTripPoint1_0, kConfigTripPoint1_0},
                                            {kTripPoint1_1, kConfigTripPoint1_1}};
  std::vector<ThermalConfig::State> states2{{kTripPoint2_0, kConfigTripPoint2_0},
                                            {kTripPoint2_1, kConfigTripPoint2_1}};
  ProcessConfig process_config =
      ProcessConfigBuilder()
          .SetDefaultVolumeCurve(
              VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume))
          .AddDeviceRoutingProfile(
              {std::nullopt,
               RoutingConfig::DeviceProfile(false, {}, false, std::move(pipeline_config))})
          .AddThermalPolicyEntry(ThermalConfig::Entry(kTargetName0, states0))
          .AddThermalPolicyEntry(ThermalConfig::Entry(kTargetName1, states1))
          .AddThermalPolicyEntry(ThermalConfig::Entry(kTargetName2, states2))
          .Build();
  fuchsia::thermal::ControllerPtr thermal_controller;
  FakeThermalController fake_thermal_controller;
  fake_thermal_controller.Bind(thermal_controller.NewRequest());

  ThermalAgent under_test(std::move(thermal_controller), process_config.thermal_config(),
                          process_config.routing_config(), ConfigCallback());

  RunLoopUntilIdle();
  EXPECT_NE(ZX_OK, fake_thermal_controller.binding_error());
  ExpectNoOtherConfigAction();
  // We expect four trip points (5 states).
  fake_thermal_controller.ExpectSubscribeCalled(
      fuchsia::thermal::ActorType::AUDIO,
      {kTripPoint0_0, kTripPoint1_0, kTripPoint1_1, kTripPoint2_1});

  // Thermal config for state 1 in effect.
  fake_thermal_controller.SetThermalState(1);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName0, kConfigTripPoint0_0);
  ExpectNoOtherConfigAction();
  fake_thermal_controller.set_thermal_state_returned() = false;

  // Thermal config for state 2 in effect.
  fake_thermal_controller.SetThermalState(2);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName1, kConfigTripPoint1_0);
  ExpectConfigAction(kTargetName2, kConfigTripPoint2_0);
  ExpectNoOtherConfigAction();
  fake_thermal_controller.set_thermal_state_returned() = false;

  // Thermal config for state 3 in effect.
  fake_thermal_controller.SetThermalState(3);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName0, kConfigTripPoint0_1);
  ExpectConfigAction(kTargetName1, kConfigTripPoint1_1);
  ExpectNoOtherConfigAction();
  fake_thermal_controller.set_thermal_state_returned() = false;

  // Thermal config for state 4 in effect.
  fake_thermal_controller.SetThermalState(4);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName2, kConfigTripPoint2_1);
  ExpectNoOtherConfigAction();
  fake_thermal_controller.set_thermal_state_returned() = false;

  // Thermal config for state 2 in effect.
  fake_thermal_controller.SetThermalState(2);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName0, kConfigTripPoint0_0);
  ExpectConfigAction(kTargetName1, kConfigTripPoint1_0);
  ExpectConfigAction(kTargetName2, kConfigTripPoint2_0);
  ExpectNoOtherConfigAction();
  fake_thermal_controller.set_thermal_state_returned() = false;

  // Nominal config restored.
  fake_thermal_controller.SetThermalState(0);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_thermal_controller.set_thermal_state_returned());
  ExpectConfigAction(kTargetName0, kNominalConfig0);
  ExpectConfigAction(kTargetName1, kNominalConfig1);
  ExpectConfigAction(kTargetName2, kNominalConfig2);
  ExpectNoOtherConfigAction();
}

}  // namespace
}  // namespace media::audio
