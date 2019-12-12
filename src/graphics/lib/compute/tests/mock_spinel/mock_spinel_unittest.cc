// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock_spinel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_spinel_test_utils.h"
#include "spinel_api_interface.h"
#include "tests/common/spinel/spinel_test_utils.h"

class MockSpinelTest : public mock_spinel::Test {
};

// Assertion macro to check that Spinel calls succeed properly.
#define ASSERT_SPN(...) ASSERT_EQ(SPN_SUCCESS, __VA_ARGS__)

TEST_F(MockSpinelTest, CreationDestruction)
{
  ASSERT_TRUE(context_);
  ASSERT_TRUE(path_builder_);
  ASSERT_TRUE(raster_builder_);
  ASSERT_TRUE(composition_);
  ASSERT_TRUE(styling_);
}

static void
buildSquarePath(
  float x, float y, float w, float h, spn_path_builder_t path_builder, spn_path_t * pHandle)
{
  ASSERT_SPN(spn_path_builder_begin(path_builder));

  ASSERT_SPN(spn_path_builder_move_to(path_builder, x, y));
  ASSERT_SPN(spn_path_builder_line_to(path_builder, x + w, y));
  ASSERT_SPN(spn_path_builder_line_to(path_builder, x + w, y + h));
  ASSERT_SPN(spn_path_builder_line_to(path_builder, x, y + h));
  ASSERT_SPN(spn_path_builder_line_to(path_builder, x, y));

  spn_path_t handle;
  ASSERT_SPN(spn_path_builder_end(path_builder, &handle));
  *pHandle = handle;
}

TEST_F(MockSpinelTest, PathBuilder)
{
  spn_path_t handle;
  buildSquarePath(16., 16., 16., 16., path_builder_, &handle);

  const mock_spinel::Path * path = mock_context()->pathFor(handle);
  ASSERT_TRUE(path);
  // clang-format off
  static const float kExpectedPathCoords[] = {
    MOCK_SPINEL_PATH_MOVE_TO_LITERAL(16, 16),
    MOCK_SPINEL_PATH_LINE_TO_LITERAL(32, 16),
    MOCK_SPINEL_PATH_LINE_TO_LITERAL(32, 32),
    MOCK_SPINEL_PATH_LINE_TO_LITERAL(16, 32),
    MOCK_SPINEL_PATH_LINE_TO_LITERAL(16, 16),
  };
  // clang-format on
  ASSERT_THAT(path->data, ::testing::ElementsAreArray(kExpectedPathCoords));

  ASSERT_SPN(spn_path_release(context_, &handle, 1));
}

TEST_F(MockSpinelTest, RasterBuilder)
{
  spn_path_t path_handles[2];
  buildSquarePath(16., 16., 16., 16., path_builder_, &path_handles[0]);
  buildSquarePath(8., 10., 24., 32., path_builder_, &path_handles[1]);

  static const spn_transform_t transforms[2] = {
    ::spinel_constants::identity_transform,
    { 2., 1., 10., 1., 2., 20., 0., 0. },  // scaling + shearing + translation
  };

  spn_raster_t handle;
  ASSERT_SPN(spn_raster_builder_begin(raster_builder_));
  ASSERT_SPN(spn_raster_builder_add(raster_builder_,
                                    path_handles,
                                    nullptr,  // transform_weakrefs,
                                    transforms,
                                    nullptr,  // clip_weakrefs,
                                    nullptr,  // clips
                                    2u));

  ASSERT_SPN(spn_raster_builder_end(raster_builder_, &handle));

  const mock_spinel::Raster * raster = mock_context()->rasterFor(handle);
  ASSERT_TRUE(raster);

  ASSERT_EQ(2u, raster->size());
  const auto & raster0 = (*raster)[0];
  const auto & raster1 = (*raster)[1];

  ASSERT_EQ(raster0.path_id, path_handles[0].handle);
  ASSERT_EQ(raster1.path_id, path_handles[1].handle);

  // clang-format on
  ASSERT_SPN_TRANSFORM_EQ(raster0.transform, transforms[0]);
  ASSERT_SPN_TRANSFORM_EQ(raster1.transform, transforms[1]);

  ASSERT_SPN_CLIP_EQ(raster0.clip, ::spinel_constants::default_clip);
  ASSERT_SPN_CLIP_EQ(raster1.clip, ::spinel_constants::default_clip);

  ASSERT_SPN(spn_path_release(context_, path_handles, 2));
}

TEST_F(MockSpinelTest, Composition)
{
  spn_path_t path_handle;
  buildSquarePath(16., 16., 16., 16., path_builder_, &path_handle);

  spn_raster_t raster_handle;
  ASSERT_SPN(spn_raster_builder_begin(raster_builder_));
  ASSERT_SPN(spn_raster_builder_add(raster_builder_,
                                    &path_handle,
                                    nullptr,  // transform_weakrefs,
                                    nullptr,  // transforms,
                                    nullptr,  // clip_weakrefs,
                                    nullptr,  // clips
                                    1u));
  ASSERT_SPN(spn_raster_builder_end(raster_builder_, &raster_handle));

  ASSERT_SPN(spn_composition_unseal(composition_));

  {
    const spn_raster_t handles[3]      = { raster_handle, raster_handle, raster_handle };
    const spn_layer_id layers[3]       = { 42u, 10u, 42u };
    const spn_txty_t   translations[3] = {
      { 0, 0 },
      { 100, 100 },
      { 50, 50 },
    };

    ASSERT_SPN(spn_composition_place(composition_, handles, layers, translations, 3u));

    ASSERT_SPN(spn_composition_seal(composition_));

    mock_spinel::Composition::LayerMap layerMap = mock_composition()->computeLayerMap();
    ASSERT_EQ(layerMap.size(), 2u);

    ASSERT_EQ(layerMap.count(10u), 1u);
    const auto & layer0 = layerMap[10u];
    ASSERT_EQ(layer0.size(), 1u);

    ASSERT_EQ(layer0[0]->raster_id, raster_handle.handle);
    ASSERT_EQ(layer0[0]->layer_id, 10u);
    ASSERT_SPN_TXTY_EQ(layer0[0]->translation, translations[1]);

    ASSERT_EQ(layerMap.count(42u), 1u);
    const auto & layer42 = layerMap[42u];
    ASSERT_EQ(layer42.size(), 2u);

    ASSERT_EQ(layer42[0]->raster_id, raster_handle.handle);
    ASSERT_EQ(layer42[0]->layer_id, 42u);
    ASSERT_SPN_TXTY_EQ(layer42[0]->translation, translations[0]);

    ASSERT_EQ(layer42[1]->raster_id, raster_handle.handle);
    ASSERT_EQ(layer42[1]->layer_id, 42u);
    ASSERT_SPN_TXTY_EQ(layer42[1]->translation, translations[2]);
  }
}
