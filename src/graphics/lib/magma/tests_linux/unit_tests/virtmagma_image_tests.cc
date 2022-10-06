// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/mman.h>

#include <gtest/gtest.h>
#include <magma/magma.h>

#include "drm_fourcc.h"

// Copied from vulkan_core.h
typedef enum VkImageUsageFlagBits {
  VK_IMAGE_USAGE_TRANSFER_SRC_BIT = 0x00000001,
  VK_IMAGE_USAGE_TRANSFER_DST_BIT = 0x00000002,
  VK_IMAGE_USAGE_SAMPLED_BIT = 0x00000004,
  VK_IMAGE_USAGE_STORAGE_BIT = 0x00000008,
  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 0x00000010,
  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT = 0x00000020,
  VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT = 0x00000040,
  VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT = 0x00000080,
  VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV = 0x00000100,
  VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT = 0x00000200,
  VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR =
      VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV,
  VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VkImageUsageFlagBits;

class MagmaImageTest : public ::testing::Test {
 public:
  static constexpr bool kDirectToDisplaySupported = false;

  void SetUp() override {
    static constexpr const char* kDevicePath = "/dev/magma0";
    int fd = open(kDevicePath, O_NONBLOCK);

    ASSERT_GE(fd, 0) << "Failed to open device " << kDevicePath << " (" << errno << ")";
    ASSERT_EQ(MAGMA_STATUS_OK, magma_device_import(fd, &device_));

    ASSERT_EQ(MAGMA_STATUS_OK, magma_create_connection2(device_, &connection_));
  }

  virtual void TearDown() override {
    if (connection_)
      magma_release_connection(connection_);
    if (device_)
      magma_device_release(device_);
  }

  magma_device_t device_ = {};
  magma_connection_t connection_ = {};
};

constexpr uint32_t kWidth = 1920;
constexpr uint32_t kHeight = 1080;
constexpr uint64_t kFormat = DRM_FORMAT_ARGB8888;

TEST_F(MagmaImageTest, CreateInvalidFormat) {
  magma_image_create_info_t create_info = {
      .drm_format = 0,
      .drm_format_modifiers = {DRM_FORMAT_MOD_INVALID},
      .width = kWidth,
      .height = kHeight,
      .flags = 0,
  };
  magma_buffer_t image;

  EXPECT_EQ(MAGMA_STATUS_INVALID_ARGS, magma_virt_create_image(connection_, &create_info, &image));
}

TEST_F(MagmaImageTest, CreateInvalidModifier) {
  magma_image_create_info_t create_info = {
      .drm_format = kFormat,
      .drm_format_modifiers = {},
      .width = kWidth,
      .height = kHeight,
      .flags = 0,
  };
  for (uint32_t i = 0; i < MAGMA_MAX_DRM_FORMAT_MODIFIERS; i++) {
    create_info.drm_format_modifiers[i] = i;
  }
  magma_buffer_t image;

  EXPECT_EQ(MAGMA_STATUS_INVALID_ARGS, magma_virt_create_image(connection_, &create_info, &image));
}

TEST_F(MagmaImageTest, CreateInvalidWidth) {
  magma_image_create_info_t create_info = {
      .drm_format = kFormat,
      .drm_format_modifiers = {DRM_FORMAT_MOD_INVALID},
      .width = std::numeric_limits<uint32_t>::max(),
      .height = kHeight,
      .flags = 0,
  };
  magma_buffer_t image;

  EXPECT_EQ(MAGMA_STATUS_INVALID_ARGS, magma_virt_create_image(connection_, &create_info, &image));
}

TEST_F(MagmaImageTest, CreateInvalidHeight) {
  magma_image_create_info_t create_info = {
      .drm_format = kFormat,
      .drm_format_modifiers = {DRM_FORMAT_MOD_INVALID},
      .width = kWidth,
      .height = std::numeric_limits<uint32_t>::max(),
      .flags = 0,
  };
  magma_buffer_t image;

  EXPECT_EQ(MAGMA_STATUS_INVALID_ARGS, magma_virt_create_image(connection_, &create_info, &image));
}

TEST_F(MagmaImageTest, CreateInvalidFlags) {
  magma_image_create_info_t create_info = {
      .drm_format = kFormat,
      .drm_format_modifiers = {DRM_FORMAT_MOD_INVALID},
      .width = kWidth,
      .height = kHeight,
      .flags = std::numeric_limits<uint32_t>::max(),
  };
  magma_buffer_t image;

  EXPECT_EQ(MAGMA_STATUS_INVALID_ARGS, magma_virt_create_image(connection_, &create_info, &image));
}

using DrmFormat = uint64_t;

class MagmaImageTestFormats : public MagmaImageTest, public testing::WithParamInterface<DrmFormat> {
 public:
  static constexpr uint8_t kBytePattern = 0xfa;

  void MapAndWrite(magma_buffer_t image) {
    magma_handle_t buffer_handle;
    ASSERT_EQ(MAGMA_STATUS_OK, magma_get_buffer_handle2(image, &buffer_handle));

    size_t length = magma_get_buffer_size(image);

    int fd = buffer_handle;
    void* addr = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 /*offset*/);
    ASSERT_NE(MAP_FAILED, addr);

    memset(addr, kBytePattern, length);

    munmap(addr, length);
    close(fd);
  }

  void MapAndCompare(magma_buffer_t image) {
    magma_handle_t buffer_handle;
    ASSERT_EQ(MAGMA_STATUS_OK, magma_get_buffer_handle2(image, &buffer_handle));

    size_t length = magma_get_buffer_size(image);

    int fd = buffer_handle;
    void* addr = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 /*offset*/);
    ASSERT_NE(MAP_FAILED, addr);

    for (uint32_t i = 0; i < length; i++) {
      EXPECT_EQ(kBytePattern, reinterpret_cast<uint8_t*>(addr)[i]);
    }

    munmap(addr, length);
    close(fd);
  }

  void ImportExportTest(uint64_t flags, uint64_t specified_modifier, uint64_t expected_modifier) {
    int fd = 0;

    {
      magma_image_create_info_t create_info = {
          .drm_format = GetParam(),
          .drm_format_modifiers = {specified_modifier, DRM_FORMAT_MOD_INVALID},
          .width = kWidth,
          .height = kHeight,
          .flags = flags,
      };
      magma_buffer_t image;

      ASSERT_EQ(MAGMA_STATUS_OK, magma_virt_create_image(connection_, &create_info, &image));

      magma_image_info_t image_info = {};
      ASSERT_EQ(MAGMA_STATUS_OK, magma_virt_get_image_info(connection_, image, &image_info));

      EXPECT_EQ(expected_modifier, image_info.drm_format_modifier);
      if (expected_modifier == DRM_FORMAT_MOD_LINEAR) {
        EXPECT_EQ(kWidth * 4, image_info.plane_strides[0]);
      }
      EXPECT_EQ(0u, image_info.plane_offsets[0]);
      if (kDirectToDisplaySupported && (flags & MAGMA_IMAGE_CREATE_FLAGS_PRESENTABLE)) {
        EXPECT_EQ(MAGMA_COHERENCY_DOMAIN_RAM, image_info.coherency_domain);
      } else {
        EXPECT_EQ(MAGMA_COHERENCY_DOMAIN_CPU, image_info.coherency_domain);
      }

      MapAndWrite(image);

      {
        magma_handle_t buffer_handle;
        EXPECT_EQ(MAGMA_STATUS_OK, magma_export(connection_, image, &buffer_handle));
        fd = buffer_handle;
      }

      magma_release_buffer(connection_, image);
    }

    EXPECT_GT(fd, 0);

    // Import into a new connection
    TearDown();
    SetUp();

    {
      magma_buffer_t image;
      magma_handle_t handle = fd;
      ASSERT_EQ(MAGMA_STATUS_OK, magma_import(connection_, handle, &image));

      magma_image_info_t image_info = {};
      ASSERT_EQ(MAGMA_STATUS_OK, magma_virt_get_image_info(connection_, image, &image_info));

      EXPECT_EQ(expected_modifier, image_info.drm_format_modifier);
      if (expected_modifier == DRM_FORMAT_MOD_LINEAR) {
        EXPECT_EQ(kWidth * 4, image_info.plane_strides[0]);
      }
      EXPECT_EQ(0u, image_info.plane_offsets[0]);
      if (kDirectToDisplaySupported && (flags & MAGMA_IMAGE_CREATE_FLAGS_PRESENTABLE)) {
        EXPECT_EQ(MAGMA_COHERENCY_DOMAIN_RAM, image_info.coherency_domain);
      } else {
        EXPECT_EQ(MAGMA_COHERENCY_DOMAIN_CPU, image_info.coherency_domain);
      }

      MapAndCompare(image);

      magma_release_buffer(connection_, image);
    }
  }
};

TEST_P(MagmaImageTestFormats, ImportExportLinear) {
  constexpr uint64_t kFlags = 0;
  constexpr uint64_t kSpecifiedModifier = DRM_FORMAT_MOD_LINEAR;
  constexpr uint64_t kExpectedModifier = DRM_FORMAT_MOD_LINEAR;
  ImportExportTest(kFlags, kSpecifiedModifier, kExpectedModifier);
}

TEST_P(MagmaImageTestFormats, ImportExportPresentableLinear) {
  constexpr uint64_t kFlags = MAGMA_IMAGE_CREATE_FLAGS_PRESENTABLE;
  constexpr uint64_t kSpecifiedModifier = DRM_FORMAT_MOD_LINEAR;
  constexpr uint64_t kExpectedModifier = DRM_FORMAT_MOD_LINEAR;
  ImportExportTest(kFlags, kSpecifiedModifier, kExpectedModifier);
}

TEST_P(MagmaImageTestFormats, ImportExportIntel) {
  constexpr uint64_t kFlags = 0;
  constexpr uint64_t kSpecifiedModifier = DRM_FORMAT_MOD_INVALID;
  constexpr uint64_t kExpectedModifier = I915_FORMAT_MOD_Y_TILED_CCS;
  ImportExportTest(kFlags, kSpecifiedModifier, kExpectedModifier);
}

TEST_P(MagmaImageTestFormats, ImportExportPresentableIntel) {
  constexpr uint64_t kFlags = MAGMA_IMAGE_CREATE_FLAGS_PRESENTABLE;
  constexpr uint64_t kSpecifiedModifier = DRM_FORMAT_MOD_INVALID;
  constexpr uint64_t kExpectedModifier = I915_FORMAT_MOD_Y_TILED;
  ImportExportTest(kFlags, kSpecifiedModifier, kExpectedModifier);
}

TEST_P(MagmaImageTestFormats, ImportExportWithUsageIntel) {
  constexpr uint64_t kFlags =
      (static_cast<uint64_t>(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
       << 32) |
      MAGMA_IMAGE_CREATE_FLAGS_VULKAN_USAGE;
  constexpr uint64_t kSpecifiedModifier = DRM_FORMAT_MOD_INVALID;
  constexpr uint64_t kExpectedModifier = I915_FORMAT_MOD_Y_TILED_CCS;
  ImportExportTest(kFlags, kSpecifiedModifier, kExpectedModifier);
}

INSTANTIATE_TEST_SUITE_P(MagmaImageTestFormats, MagmaImageTestFormats,
                         ::testing::Values(DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888),
                         [](testing::TestParamInfo<uint64_t> info) {
                           switch (info.param) {
                             case DRM_FORMAT_ARGB8888:
                               return "DRM_FORMAT_ARGB8888";
                             case DRM_FORMAT_XRGB8888:
                               return "DRM_FORMAT_XRGB8888";
                             default:
                               return "Unknown format";
                           }
                         });
