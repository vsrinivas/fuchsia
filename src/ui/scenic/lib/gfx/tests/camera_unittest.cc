// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/camera.h"

#include "gtest/gtest.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "src/ui/lib/escher/scene/viewing_volume.h"
#include "src/ui/lib/escher/util/epsilon_compare.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

#include <glm/gtc/type_ptr.hpp>

namespace {

using namespace scenic_impl;
using namespace scenic_impl::gfx;
using namespace scenic_impl::gfx::test;

constexpr ResourceId scene_id = 1;
constexpr ResourceId camera_id = 2;

using CameraTest = SessionTest;

TEST_F(CameraTest, Create) {
  constexpr ResourceId invalid_id = 0;
  Apply(scenic::NewCreateSceneCmd(scene_id));

  EXPECT_TRUE(Apply(scenic::NewCreateCameraCmd(camera_id, scene_id)));
  EXPECT_FALSE(Apply(scenic::NewCreateCameraCmd(camera_id, invalid_id)));
}

TEST_F(CameraTest, SetClipSpaceTransform) {
  Apply(scenic::NewCreateSceneCmd(scene_id));
  Apply(scenic::NewCreateCameraCmd(camera_id, scene_id));

  auto camera = session()->resources()->FindResource<Camera>(camera_id);
  ASSERT_TRUE(camera);

  static const escher::ViewingVolume viewing_volume(1000, 1000, -1000, 0);

  escher::Camera escher_camera = camera->GetEscherCamera(viewing_volume);
  glm::mat4 transform0 = escher_camera.transform();
  glm::mat4 projection0 = escher_camera.projection();

  // Clip-space transform should tweak the projection matrix.
  EXPECT_TRUE(Apply(scenic::NewSetCameraClipSpaceTransformCmd(camera_id, 1, -2, 3)));

  escher_camera = camera->GetEscherCamera(viewing_volume);
  EXPECT_TRUE(escher::CompareMatrix(transform0, escher_camera.transform()));
  EXPECT_FALSE(escher::CompareMatrix(projection0, escher_camera.projection()));

  // Restore clip-space transform to identity.
  EXPECT_TRUE(Apply(scenic::NewSetCameraClipSpaceTransformCmd(camera_id, 0, 0, 1)));

  escher_camera = camera->GetEscherCamera(viewing_volume);
  EXPECT_TRUE(escher::CompareMatrix(transform0, escher_camera.transform()));
  EXPECT_TRUE(escher::CompareMatrix(projection0, escher_camera.projection()));
}

}  // namespace
