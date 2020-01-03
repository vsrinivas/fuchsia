// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"

#include "gtest/gtest.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "src/ui/scenic/lib/gfx/resources/lights/ambient_light.h"
#include "src/ui/scenic/lib/gfx/resources/lights/directional_light.h"
#include "src/ui/scenic/lib/gfx/resources/lights/point_light.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using SceneTest = SessionTest;

TEST_F(SceneTest, Lighting) {
  const ResourceId kSceneId = 1;
  const ResourceId kAmbientLightId = 2;
  const ResourceId kDirectionalLightId = 3;
  const ResourceId kPointLightId = 4;
  const ResourceId kPointLight2Id = 5;

  EXPECT_TRUE(Apply(scenic::NewCreateSceneCmd(kSceneId)));
  EXPECT_TRUE(Apply(scenic::NewCreateAmbientLightCmd(kAmbientLightId)));
  EXPECT_TRUE(Apply(scenic::NewCreateDirectionalLightCmd(kDirectionalLightId)));
  EXPECT_TRUE(Apply(scenic::NewCreatePointLightCmd(kPointLightId)));
  EXPECT_TRUE(Apply(scenic::NewCreatePointLightCmd(kPointLight2Id)));

  auto scene = FindResource<Scene>(kSceneId);
  auto ambient = FindResource<AmbientLight>(kAmbientLightId);
  auto directional = FindResource<DirectionalLight>(kDirectionalLightId);
  auto point = FindResource<PointLight>(kPointLightId);
  auto point2 = FindResource<PointLight>(kPointLight2Id);
  EXPECT_EQ(0U, scene->ambient_lights().size());
  EXPECT_EQ(0U, scene->directional_lights().size());
  EXPECT_EQ(0U, scene->point_lights().size());

  EXPECT_TRUE(Apply(scenic::NewSceneAddAmbientLightCmd(kSceneId, kAmbientLightId)));
  EXPECT_TRUE(Apply(scenic::NewSceneAddDirectionalLightCmd(kSceneId, kDirectionalLightId)));
  EXPECT_TRUE(Apply(scenic::NewSceneAddPointLightCmd(kSceneId, kPointLightId)));
  EXPECT_TRUE(Apply(scenic::NewAddLightCmd(kSceneId, kPointLight2Id)));
  EXPECT_EQ(1U, scene->ambient_lights().size());
  EXPECT_EQ(1U, scene->directional_lights().size());
  EXPECT_EQ(2U, scene->point_lights().size());

  // Check that light color defaults to (0,0,0), and that we can set it
  // to something different.
  EXPECT_EQ(ambient->color(), glm::vec3(0.f, 0.f, 0.f));
  EXPECT_EQ(directional->color(), glm::vec3(0.f, 0.f, 0.f));
  EXPECT_EQ(point->color(), glm::vec3(0.f, 0.f, 0.f));
  EXPECT_EQ(point2->color(), glm::vec3(0.f, 0.f, 0.f));
  {
    const std::array<float, 3> rgb = {1.f, 0.8f, 0.8f};
    EXPECT_TRUE(Apply(scenic::NewSetLightColorCmd(kAmbientLightId, rgb)));
    EXPECT_EQ(ambient->color().x, rgb[0]);
    EXPECT_EQ(ambient->color().y, rgb[1]);
    EXPECT_EQ(ambient->color().z, rgb[2]);
  }

  // Position and falloff of point light.
  {
    const std::array<float, 3> pos = {111.f, 222.f, 333.f};
    EXPECT_TRUE(Apply(scenic::NewSetPointLightPositionCmd(kPointLightId, pos)));
    EXPECT_EQ(point->position().x, pos[0]);
    EXPECT_EQ(point->position().y, pos[1]);
    EXPECT_EQ(point->position().z, pos[2]);
    const float kFalloff = 0.6f;
    EXPECT_TRUE(Apply(scenic::NewSetPointLightFalloffCmd(kPointLightId, kFalloff)));
    EXPECT_EQ(kFalloff, point->falloff());
  }
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
