// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_image.h"

#include <gtest/gtest.h>

#include "drm_fourcc.h"

class MagmaImageTesting : public ::testing::Test {};

constexpr uint64_t kWidth = 1920;
constexpr uint64_t kHeight = 1080;
constexpr uint32_t kFormat = DRM_FORMAT_ARGB8888;

TEST_F(MagmaImageTesting, SpecifyLinear) {
  uint32_t physical_device_index = 0;
  zx::vmo buffer;
  zx::eventpair token;

  magma_image_create_info_t create_info = {
      .drm_format = kFormat,
      .drm_format_modifiers = {DRM_FORMAT_MOD_LINEAR, DRM_FORMAT_MOD_INVALID},
      .width = kWidth,
      .height = kHeight,
      .flags = 0,
  };

  magma_image_info_t image_info = {};
  ASSERT_EQ(MAGMA_STATUS_OK, magma_image::CreateDrmImage(physical_device_index, &create_info,
                                                         &image_info, &buffer, &token));

  EXPECT_EQ(DRM_FORMAT_MOD_LINEAR, image_info.drm_format_modifier);
  EXPECT_EQ(kWidth * 4u, image_info.plane_strides[0]);
  EXPECT_EQ(0u, image_info.plane_offsets[0]);
  EXPECT_EQ(MAGMA_COHERENCY_DOMAIN_CPU, image_info.coherency_domain);
  EXPECT_FALSE(token);
}

TEST_F(MagmaImageTesting, SpecifyIntelX) {
  uint32_t physical_device_index = 0;
  zx::vmo buffer;
  zx::eventpair token;

  magma_image_create_info_t create_info = {
      .drm_format = kFormat,
      .drm_format_modifiers = {I915_FORMAT_MOD_X_TILED, DRM_FORMAT_MOD_INVALID},
      .width = kWidth,
      .height = kHeight,
      .flags = 0,
  };

  magma_image_info_t image_info = {};
  ASSERT_EQ(MAGMA_STATUS_OK, magma_image::CreateDrmImage(physical_device_index, &create_info,
                                                         &image_info, &buffer, &token));

  EXPECT_EQ(I915_FORMAT_MOD_X_TILED, image_info.drm_format_modifier);
  EXPECT_EQ(7680u, image_info.plane_strides[0]);
  EXPECT_EQ(0u, image_info.plane_offsets[0]);
  EXPECT_EQ(MAGMA_COHERENCY_DOMAIN_CPU, image_info.coherency_domain);
  EXPECT_FALSE(token);
}

TEST_F(MagmaImageTesting, SpecifyIntelY) {
  uint32_t physical_device_index = 0;
  zx::vmo buffer;
  zx::eventpair token;

  magma_image_create_info_t create_info = {
      .drm_format = kFormat,
      .drm_format_modifiers = {I915_FORMAT_MOD_Y_TILED, DRM_FORMAT_MOD_INVALID},
      .width = kWidth,
      .height = kHeight,
      .flags = 0,
  };

  magma_image_info_t image_info = {};
  ASSERT_EQ(MAGMA_STATUS_OK, magma_image::CreateDrmImage(physical_device_index, &create_info,
                                                         &image_info, &buffer, &token));

  EXPECT_EQ(I915_FORMAT_MOD_Y_TILED, image_info.drm_format_modifier);
  EXPECT_EQ(7680u, image_info.plane_strides[0]);
  EXPECT_EQ(0u, image_info.plane_offsets[0]);
  EXPECT_EQ(MAGMA_COHERENCY_DOMAIN_CPU, image_info.coherency_domain);
  EXPECT_FALSE(token);
}

TEST_F(MagmaImageTesting, SpecifyIntelYf) {
  uint32_t physical_device_index = 0;
  zx::vmo buffer;
  zx::eventpair token;

  magma_image_create_info_t create_info = {
      .drm_format = kFormat,
      .drm_format_modifiers = {I915_FORMAT_MOD_Yf_TILED, DRM_FORMAT_MOD_INVALID},
      .width = kWidth,
      .height = kHeight,
      .flags = 0,
  };

  magma_image_info_t image_info = {};
  ASSERT_EQ(MAGMA_STATUS_INVALID_ARGS,
            magma_image::CreateDrmImage(physical_device_index, &create_info, &image_info, &buffer,
                                        &token));
  EXPECT_FALSE(token);
}

TEST_F(MagmaImageTesting, IntelMany) {
  uint32_t physical_device_index = 0;
  zx::vmo buffer;
  zx::eventpair token;

  magma_image_create_info_t create_info = {
      .drm_format = kFormat,
      .drm_format_modifiers = {DRM_FORMAT_MOD_LINEAR, I915_FORMAT_MOD_X_TILED,
                               I915_FORMAT_MOD_Y_TILED, I915_FORMAT_MOD_Yf_TILED,
                               DRM_FORMAT_MOD_INVALID},
      .width = kWidth,
      .height = kHeight,
      .flags = 0,
  };

  magma_image_info_t image_info = {};
  ASSERT_EQ(MAGMA_STATUS_OK, magma_image::CreateDrmImage(physical_device_index, &create_info,
                                                         &image_info, &buffer, &token));

  EXPECT_EQ(I915_FORMAT_MOD_Y_TILED, image_info.drm_format_modifier);
  EXPECT_EQ(7680u, image_info.plane_strides[0]);
  EXPECT_EQ(0u, image_info.plane_offsets[0]);
  EXPECT_EQ(MAGMA_COHERENCY_DOMAIN_CPU, image_info.coherency_domain);
  EXPECT_FALSE(token);
}

TEST_F(MagmaImageTesting, IntelNone) {
  uint32_t physical_device_index = 0;
  magma_image_info_t image_info = {};
  zx::vmo buffer;
  zx::eventpair token;

  magma_image_create_info_t create_info = {
      .drm_format = kFormat,
      .drm_format_modifiers = {DRM_FORMAT_MOD_INVALID},
      .width = kWidth,
      .height = kHeight,
      .flags = 0,
  };

  ASSERT_EQ(MAGMA_STATUS_OK, magma_image::CreateDrmImage(physical_device_index, &create_info,
                                                         &image_info, &buffer, &token));

  EXPECT_EQ(I915_FORMAT_MOD_Y_TILED, image_info.drm_format_modifier);
  EXPECT_EQ(7680u, image_info.plane_strides[0]);
  EXPECT_EQ(0u, image_info.plane_offsets[0]);
  EXPECT_EQ(MAGMA_COHERENCY_DOMAIN_CPU, image_info.coherency_domain);
  EXPECT_FALSE(token);
}

TEST_F(MagmaImageTesting, IntelNonePresentable) {
  uint32_t physical_device_index = 0;
  magma_image_info_t image_info = {};
  zx::vmo buffer;
  zx::eventpair token;

  magma_image_create_info_t create_info = {
      .drm_format = kFormat,
      .drm_format_modifiers = {DRM_FORMAT_MOD_INVALID},
      .width = kWidth,
      .height = kHeight,
      .flags = MAGMA_IMAGE_CREATE_FLAGS_PRESENTABLE,
  };

  ASSERT_EQ(MAGMA_STATUS_OK, magma_image::CreateDrmImage(physical_device_index, &create_info,
                                                         &image_info, &buffer, &token));

  if (image_info.drm_format_modifier != I915_FORMAT_MOD_Y_TILED)
    EXPECT_EQ(image_info.drm_format_modifier, I915_FORMAT_MOD_X_TILED);
  EXPECT_EQ(7680u, image_info.plane_strides[0]);
  EXPECT_EQ(0u, image_info.plane_offsets[0]);
  EXPECT_EQ(MAGMA_COHERENCY_DOMAIN_CPU, image_info.coherency_domain);
  EXPECT_TRUE(token);
}
