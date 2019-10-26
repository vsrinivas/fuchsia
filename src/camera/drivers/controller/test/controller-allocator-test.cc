// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>

#include <fbl/auto_call.h>

#include "src/camera/drivers/controller/configs/sherlock/monitoring-config.h"
#include "src/camera/drivers/controller/memory_allocation.h"

// NOTE: In this test, we are actually just unit testing
// the sysmem allocation using different constraints.
namespace camera {

class ControllerMemoryAllocatorTest : public gtest::TestLoopFixture {
 public:
  ControllerMemoryAllocatorTest() : context_(sys::ComponentContext::Create()) {}

  void SetUp() override {
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(sysmem_allocator_.NewRequest()));
    controller_memory_allocator_ =
        std::make_unique<ControllerMemoryAllocator>(std::move(sysmem_allocator_));
  }

  void TearDown() override {
    context_ = nullptr;
    sysmem_allocator_ = nullptr;
  }

  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  std::unique_ptr<ControllerMemoryAllocator> controller_memory_allocator_;
};

// Validate FR --> GDC1 --> OutputStreamMLDS
// Buffer collection constraints.
TEST_F(ControllerMemoryAllocatorTest, MonitorConfigFR) {
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info;
  EXPECT_EQ(ZX_OK,
            controller_memory_allocator_->AllocateSharedMemory(
                MonitorConfigFullResConstraints(), Gdc1Constraints(), &buffer_collection_info));
  EXPECT_EQ(buffer_collection_info.buffer_count, kOutputStreamMlDSMinBufferForCamping);
  EXPECT_TRUE(buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
  EXPECT_GT(buffer_collection_info.settings.buffer_settings.size_bytes,
            kOutputStreamMlFRHeight * kOutputStreamMlFRWidth);
  EXPECT_TRUE(buffer_collection_info.settings.has_image_format_constraints);
  EXPECT_EQ(fuchsia::sysmem::PixelFormatType::NV12,
            buffer_collection_info.settings.image_format_constraints.pixel_format.type);
  EXPECT_EQ(kOutputStreamMlFRHeight,
            buffer_collection_info.settings.image_format_constraints.max_coded_height);
  EXPECT_EQ(kOutputStreamMlFRWidth,
            buffer_collection_info.settings.image_format_constraints.max_coded_width);
  EXPECT_EQ(kISPPerRowDivisor,
            buffer_collection_info.settings.image_format_constraints.bytes_per_row_divisor);
  for (uint32_t i = 0; i < buffer_collection_info.buffer_count; i++) {
    EXPECT_TRUE(buffer_collection_info.buffers.at(i).vmo.is_valid());
  }
  EXPECT_FALSE(
      buffer_collection_info.buffers.at(buffer_collection_info.buffer_count).vmo.is_valid());
}

// Validate DS --> GDC2 --> (GE2D) --> OutputStreamMonitoring
// This validates only DS --> GDC2
// Buffer collection constraints.
TEST_F(ControllerMemoryAllocatorTest, MonitorConfigDS) {
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info;
  EXPECT_EQ(ZX_OK, controller_memory_allocator_->AllocateSharedMemory(
                       MonitorConfigDownScaledResConstraints(), Gdc2Constraints(),
                       &buffer_collection_info));
  EXPECT_EQ(buffer_collection_info.buffer_count, kOutputStreamMonitoringMinBufferForCamping);
  EXPECT_TRUE(buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
  EXPECT_GT(buffer_collection_info.settings.buffer_settings.size_bytes,
            kOutputStreamDSHeight * kOutputStreamDSWidth);
  EXPECT_TRUE(buffer_collection_info.settings.has_image_format_constraints);
  EXPECT_EQ(fuchsia::sysmem::PixelFormatType::NV12,
            buffer_collection_info.settings.image_format_constraints.pixel_format.type);
  EXPECT_EQ(kOutputStreamDSHeight,
            buffer_collection_info.settings.image_format_constraints.min_coded_height);
  EXPECT_EQ(kOutputStreamDSWidth,
            buffer_collection_info.settings.image_format_constraints.min_coded_width);
  EXPECT_EQ(kISPPerRowDivisor,
            buffer_collection_info.settings.image_format_constraints.bytes_per_row_divisor);
  for (uint32_t i = 0; i < buffer_collection_info.buffer_count; i++) {
    EXPECT_TRUE(buffer_collection_info.buffers.at(i).vmo.is_valid());
  }
  EXPECT_FALSE(
      buffer_collection_info.buffers.at(buffer_collection_info.buffer_count).vmo.is_valid());
}

}  // namespace camera
