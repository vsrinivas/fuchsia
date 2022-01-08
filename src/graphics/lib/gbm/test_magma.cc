// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <drm_fourcc.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <gtest/gtest.h>

#include "gbm.h"

class GbmDevice {
 public:
  void SetUp() {
    fd_ = open("/dev/magma0", O_RDWR | O_CLOEXEC);
    ASSERT_GE(fd_, 0);

    device_ = gbm_create_device(fd_);
    ASSERT_TRUE(device_);
  }

  void TearDown() {
    gbm_device_destroy(device_);
    device_ = nullptr;

    close(fd_);
    fd_ = -1;
  }

  struct gbm_device *device() {
    return device_;
  }

  int fd_ = -1;
  struct gbm_device *device_ = nullptr;
};

class MagmaGbmTest : public testing::Test {
 public:
  void SetUp() override { gbm_.SetUp(); }

  void TearDown() override { gbm_.TearDown(); }

  struct gbm_device *device() {
    return gbm_.device();
  }

  GbmDevice gbm_;
};

constexpr uint32_t kDefaultWidth = 1920;
constexpr uint32_t kDefaultHeight = 1080;
constexpr uint32_t kDefaultFormat = GBM_FORMAT_ARGB8888;

TEST_F(MagmaGbmTest, CreateLinear) {
  std::vector<uint64_t> modifiers{DRM_FORMAT_MOD_LINEAR};
  struct gbm_bo *bo =
      gbm_bo_create_with_modifiers(device(), kDefaultWidth, kDefaultHeight, kDefaultFormat,
                                   modifiers.data(), static_cast<uint32_t>(modifiers.size()));
  ASSERT_TRUE(bo);
  EXPECT_EQ(DRM_FORMAT_MOD_LINEAR, gbm_bo_get_modifier(bo));
  gbm_bo_destroy(bo);
}

TEST_F(MagmaGbmTest, CreateIntelX) {
  std::vector<uint64_t> modifiers{I915_FORMAT_MOD_X_TILED};
  struct gbm_bo *bo =
      gbm_bo_create_with_modifiers(device(), kDefaultWidth, kDefaultHeight, kDefaultFormat,
                                   modifiers.data(), static_cast<uint32_t>(modifiers.size()));
  ASSERT_TRUE(bo);
  EXPECT_EQ(I915_FORMAT_MOD_X_TILED, gbm_bo_get_modifier(bo));
  gbm_bo_destroy(bo);
}

TEST_F(MagmaGbmTest, CreateIntelY) {
  std::vector<uint64_t> modifiers{I915_FORMAT_MOD_Y_TILED};
  struct gbm_bo *bo =
      gbm_bo_create_with_modifiers(device(), kDefaultWidth, kDefaultHeight, kDefaultFormat,
                                   modifiers.data(), static_cast<uint32_t>(modifiers.size()));
  ASSERT_TRUE(bo);
  EXPECT_EQ(I915_FORMAT_MOD_Y_TILED, gbm_bo_get_modifier(bo));
  gbm_bo_destroy(bo);
}

TEST_F(MagmaGbmTest, CreateIntelBest) {
  std::vector<uint64_t> modifiers{DRM_FORMAT_MOD_LINEAR, I915_FORMAT_MOD_X_TILED,
                                  I915_FORMAT_MOD_Y_TILED};
  struct gbm_bo *bo =
      gbm_bo_create_with_modifiers(device(), kDefaultWidth, kDefaultHeight, kDefaultFormat,
                                   modifiers.data(), static_cast<uint32_t>(modifiers.size()));
  ASSERT_TRUE(bo);
  EXPECT_EQ(I915_FORMAT_MOD_Y_TILED, gbm_bo_get_modifier(bo));
  gbm_bo_destroy(bo);
}

class MagmaGbmTestWithUsage : public MagmaGbmTest, public testing::WithParamInterface<uint32_t> {};

TEST_P(MagmaGbmTestWithUsage, Create) {
  uint32_t usage = GetParam();

  struct gbm_bo *bo = gbm_bo_create(device(), kDefaultWidth, kDefaultHeight, kDefaultFormat, usage);
  ASSERT_TRUE(bo);

  if (usage & GBM_BO_USE_LINEAR) {
    EXPECT_EQ(DRM_FORMAT_MOD_LINEAR, gbm_bo_get_modifier(bo));
  } else {
    EXPECT_EQ(I915_FORMAT_MOD_Y_TILED, gbm_bo_get_modifier(bo));
  }

  gbm_bo_destroy(bo);
}

TEST_P(MagmaGbmTestWithUsage, Write) {
  uint32_t usage = GetParam();

  struct gbm_bo *bo = gbm_bo_create(device(), kDefaultWidth, kDefaultHeight, kDefaultFormat, usage);
  ASSERT_TRUE(bo);

  if (usage & GBM_BO_USE_LINEAR) {
    EXPECT_EQ(DRM_FORMAT_MOD_LINEAR, gbm_bo_get_modifier(bo));
  } else {
    EXPECT_EQ(I915_FORMAT_MOD_Y_TILED, gbm_bo_get_modifier(bo));
  }

  {
    size_t size = kDefaultHeight * gbm_bo_get_stride(bo);
    void *transfer = malloc(size);

    // Write first line - zeros
    for (uint32_t i = 0; i < kDefaultWidth; i++) {
      reinterpret_cast<uint32_t *>(transfer)[i] = 0;
    }

    // Write remainder of lines
    for (uint32_t i = kDefaultWidth; i < kDefaultWidth * kDefaultHeight; i++) {
      reinterpret_cast<uint32_t *>(transfer)[i] = i;
    }

    gbm_bo_write(bo, transfer, size);

    free(transfer);
  }
  {
    uint32_t stride;
    void *map_data;

    void *addr = gbm_bo_map(bo, 0, 0, kDefaultWidth, 1, GBM_BO_TRANSFER_READ, &stride, &map_data);
    ASSERT_NE(addr, MAP_FAILED);

    for (uint32_t i = 0; i < kDefaultWidth; i++) {
      EXPECT_EQ(reinterpret_cast<uint32_t *>(addr)[i], 0u);
    }

    gbm_bo_unmap(bo, map_data);
  }
  {
    uint32_t stride;
    void *map_data;

    void *addr = gbm_bo_map(bo, 0, 1, kDefaultWidth, kDefaultHeight - 1, GBM_BO_TRANSFER_READ,
                            &stride, &map_data);
    ASSERT_NE(addr, MAP_FAILED);

    for (uint32_t i = 0; i < kDefaultWidth * (kDefaultHeight - 1); i++) {
      EXPECT_EQ(reinterpret_cast<uint32_t *>(addr)[i], kDefaultWidth + i);
    }

    gbm_bo_unmap(bo, map_data);
  }

  gbm_bo_destroy(bo);
}

TEST_P(MagmaGbmTestWithUsage, Import) {
  GbmDevice gbm2;
  gbm2.SetUp();

  constexpr uint32_t kPattern = 0xabcd1234;

  struct gbm_bo *bo =
      gbm_bo_create(device(), kDefaultWidth, kDefaultHeight, kDefaultFormat, GetParam());
  ASSERT_TRUE(bo);

  {
    uint32_t stride;
    void *map_data;
    void *addr = gbm_bo_map(bo, 0, 0, kDefaultWidth, kDefaultHeight, GBM_BO_TRANSFER_WRITE, &stride,
                            &map_data);
    ASSERT_NE(addr, MAP_FAILED);
    *reinterpret_cast<uint32_t *>(addr) = kPattern;
    gbm_bo_unmap(bo, map_data);
  }

  {
    // Import with specified stride (could be incorrect)
    constexpr uint32_t kImportStride = 123;
    // Import usage doesn't matter
    constexpr uint32_t kImportUsage = GBM_BO_USE_RENDERING;
    struct gbm_import_fd_data import;
    import.fd = gbm_bo_get_fd(bo);
    import.format = gbm_bo_get_format(bo);
    import.width = gbm_bo_get_width(bo);
    import.height = gbm_bo_get_height(bo);
    import.stride = kImportStride;
    EXPECT_GE(import.fd, 0);
    EXPECT_EQ(import.width, kDefaultWidth);
    EXPECT_EQ(import.height, kDefaultHeight);
    EXPECT_EQ(import.format, kDefaultFormat);

    struct gbm_bo *bo2 = gbm_bo_import(gbm2.device(), GBM_BO_IMPORT_FD, &import, kImportUsage);
    ASSERT_TRUE(bo2);

    EXPECT_EQ(gbm_bo_get_width(bo), gbm_bo_get_width(bo2));
    EXPECT_EQ(gbm_bo_get_height(bo), gbm_bo_get_height(bo2));
    EXPECT_EQ(kImportStride, gbm_bo_get_stride(bo2));
    EXPECT_EQ(gbm_bo_get_format(bo), gbm_bo_get_format(bo2));
    EXPECT_EQ(gbm_bo_get_modifier(bo), gbm_bo_get_modifier(bo2));
    EXPECT_NE(DRM_FORMAT_MOD_INVALID, gbm_bo_get_modifier(bo2));

    {
      uint32_t stride;
      void *map_data;
      void *addr = gbm_bo_map(bo2, 0, 0, kDefaultWidth, kDefaultHeight, GBM_BO_TRANSFER_READ,
                              &stride, &map_data);
      ASSERT_NE(addr, MAP_FAILED);
      EXPECT_EQ(*reinterpret_cast<uint32_t *>(addr), kPattern);
      gbm_bo_unmap(bo, map_data);
    }

    gbm_bo_destroy(bo2);
  }
  {
    // Import with 0 stride
    constexpr uint32_t kImportStride = 0;
    // Import usage doesn't matter
    constexpr uint32_t kImportUsage = GBM_BO_USE_RENDERING;
    struct gbm_import_fd_data import;
    import.format = gbm_bo_get_format(bo);
    import.fd = gbm_bo_get_fd(bo);
    import.width = gbm_bo_get_width(bo);
    import.height = gbm_bo_get_height(bo);
    import.stride = kImportStride;
    EXPECT_GE(import.fd, 0);
    EXPECT_EQ(import.width, kDefaultWidth);
    EXPECT_EQ(import.height, kDefaultHeight);
    EXPECT_EQ(import.format, kDefaultFormat);

    struct gbm_bo *bo2 = gbm_bo_import(gbm2.device(), GBM_BO_IMPORT_FD, &import, kImportUsage);
    ASSERT_TRUE(bo2);

    EXPECT_EQ(gbm_bo_get_width(bo), gbm_bo_get_width(bo2));
    EXPECT_EQ(gbm_bo_get_height(bo), gbm_bo_get_height(bo2));
    EXPECT_EQ(gbm_bo_get_stride(bo), gbm_bo_get_stride(bo2));
    EXPECT_EQ(gbm_bo_get_format(bo), gbm_bo_get_format(bo2));
    EXPECT_EQ(gbm_bo_get_modifier(bo), gbm_bo_get_modifier(bo2));
    EXPECT_NE(DRM_FORMAT_MOD_INVALID, gbm_bo_get_modifier(bo2));

    {
      uint32_t stride;
      void *map_data;
      void *addr = gbm_bo_map(bo2, 0, 0, kDefaultWidth, kDefaultHeight, GBM_BO_TRANSFER_READ,
                              &stride, &map_data);
      ASSERT_NE(addr, MAP_FAILED);
      EXPECT_EQ(*reinterpret_cast<uint32_t *>(addr), kPattern);
      gbm_bo_unmap(bo, map_data);
    }

    gbm_bo_destroy(bo2);
  }

  gbm_bo_destroy(bo);

  gbm2.TearDown();
}

INSTANTIATE_TEST_SUITE_P(
    MagmaGbmTestWithUsage, MagmaGbmTestWithUsage,
    ::testing::Values(GBM_BO_USE_RENDERING, GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR,
                      GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT,
                      GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR | GBM_BO_USE_SCANOUT,
                      GBM_BO_USE_LINEAR),
    [](testing::TestParamInfo<MagmaGbmTestWithUsage::ParamType> info) {
      if (info.param == GBM_BO_USE_RENDERING)
        return "GBM_BO_USE_RENDERING";
      if (info.param == (GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR))
        return "GBM_BO_USE_RENDERING_GBM_BO_USE_LINEAR";
      if (info.param == (GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT))
        return "GBM_BO_USE_RENDERING_GBM_BO_USE_SCANOUT";
      if (info.param == (GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR | GBM_BO_USE_SCANOUT))
        return "GBM_BO_USE_RENDERING_GBM_BO_USE_LINEAR_GBM_BO_USE_SCANOUT";
      if (info.param == GBM_BO_USE_LINEAR)
        return "GBM_BO_USE_LINEAR";
      return "Unknown";
    });
