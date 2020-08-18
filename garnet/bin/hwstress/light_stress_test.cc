// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "light_stress.h"

#include <fuchsia/hardware/light/cpp/fidl_test_base.h>

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "testing_util.h"

using ::testing::ElementsAre;

namespace hwstress {
namespace {

class FakeLightServer : public fuchsia::hardware::light::testing::Light_TestBase {
 public:
  struct Light {
    std::string name;
    fuchsia::hardware::light::Capability capability;
    uint8_t brightness;
  };

  explicit FakeLightServer(std::vector<Light> lights) : lights_(std::move(lights)) {}

  // Return internal list of lights.
  const std::vector<Light>& lights() { return lights_; }

  // Implementation of |Light| methods.
  void GetNumLights(GetNumLightsCallback callback) override { return callback(lights_.size()); }
  void GetInfo(uint32_t index, GetInfoCallback callback) override {
    fuchsia::hardware::light::Light_GetInfo_Response response;
    response.info.capability = lights_.at(index).capability;
    response.info.name = lights_.at(index).name;
    callback(fuchsia::hardware::light::Light_GetInfo_Result::WithResponse(std::move(response)));
  }
  void SetBrightnessValue(uint32_t index, uint8_t value,
                          SetBrightnessValueCallback callback) override {
    lights_.at(index).brightness = value;
    callback(fuchsia::hardware::light::Light_SetBrightnessValue_Result::WithResponse(
        fuchsia::hardware::light::Light_SetBrightnessValue_Response()));
  }

  // Callback when a unimplemented FIDL method is called.
  void NotImplemented_(const std::string& name) override {
    ZX_PANIC("Unimplemented: %s", name.c_str());
  }

 private:
  std::vector<Light> lights_;
};

TEST(LightStress, GetLights) {
  // Create a light server exposing three lights.
  FakeLightServer server{{
      FakeLightServer::Light{
          .name = "A",
          .capability = fuchsia::hardware::light::Capability::BRIGHTNESS,
      },
      FakeLightServer::Light{
          .name = "unsupported",
          .capability = fuchsia::hardware::light::Capability::SIMPLE,
      },
      FakeLightServer::Light{
          .name = "B",
          .capability = fuchsia::hardware::light::Capability::BRIGHTNESS,
      },
  }};

  // Create a connection to the server.
  auto factory = std::make_unique<testing::LoopbackConnectionFactory>();
  auto client = factory->CreateSyncPtrTo<fuchsia::hardware::light::Light>(&server);

  // Query light server information.
  std::vector<LightInfo> lights = GetLights(client).value();

  // Ensure we detected the two supported lights, and the index of each is correct.
  EXPECT_THAT(lights, ElementsAre(LightInfo{"A", 0}, LightInfo{"B", 2}));
}

TEST(LightStress, TurnLightOnOff) {
  // Create a light server exposing a single light.
  FakeLightServer server{{
      FakeLightServer::Light{
          .name = "A",
          .capability = fuchsia::hardware::light::Capability::BRIGHTNESS,
      },
  }};

  // Create a connection to the server.
  auto factory = std::make_unique<testing::LoopbackConnectionFactory>();
  auto client = factory->CreateSyncPtrTo<fuchsia::hardware::light::Light>(&server);

  // Turn the light on.
  ASSERT_TRUE(TurnOnLight(client, 0).is_ok());
  EXPECT_EQ(server.lights().at(0).brightness, 255);

  // Turn the light off.
  ASSERT_TRUE(TurnOffLight(client, 0).is_ok());
  EXPECT_EQ(server.lights().at(0).brightness, 0);
}

}  // namespace
}  // namespace hwstress
