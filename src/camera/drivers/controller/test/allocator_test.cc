// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>

#include "src/camera/drivers/controller/memory_allocation.h"
#include "src/camera/drivers/controller/pipeline_manager.h"
#include "src/camera/drivers/controller/sherlock/common_util.h"
#include "src/camera/drivers/controller/sherlock/monitoring_config.h"
#include "src/camera/drivers/controller/sherlock/video_conferencing_config.h"
#include "src/camera/drivers/controller/test/fake_gdc.h"
#include "src/camera/drivers/controller/test/fake_isp.h"
#include "src/camera/lib/format_conversion/buffer_collection_helper.h"
#include "src/camera/lib/format_conversion/format_conversion.h"

// NOTE: In this test, we are actually just unit testing
// the sysmem allocation using different constraints.
namespace camera {

class ControllerMemoryAllocatorTest : public gtest::TestLoopFixture {
 public:
  ControllerMemoryAllocatorTest()
      : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

  void SetUp() override {
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(sysmem_allocator_.NewRequest()));
    ASSERT_EQ(ZX_OK, zx::event::create(0, &event_));

    controller_memory_allocator_ =
        std::make_unique<ControllerMemoryAllocator>(std::move(sysmem_allocator_));
    pipeline_manager_ =
        std::make_unique<PipelineManager>(fake_ddk::kFakeParent, dispatcher(), isp_, gdc_, ge2d_,
                                          std::move(sysmem_allocator1_), event_);
  }

  void TearDown() override {
    context_ = nullptr;
    sysmem_allocator_ = nullptr;
  }

  zx::event event_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  std::unique_ptr<ControllerMemoryAllocator> controller_memory_allocator_;
  std::unique_ptr<camera::PipelineManager> pipeline_manager_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator1_;
  ddk::IspProtocolClient isp_;
  ddk::GdcProtocolClient gdc_;
  ddk::Ge2dProtocolClient ge2d_;
};

// Validate FR --> GDC1 --> OutputStreamMLDS
// Buffer collection constraints.
TEST_F(ControllerMemoryAllocatorTest, MonitorConfigFR) {
  auto internal_config = MonitorConfigFullRes();
  auto fr_constraints = internal_config.output_constraints;
  auto gdc1_constraints = internal_config.child_nodes[1].input_constraints;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info;
  std::vector<fuchsia::sysmem::BufferCollectionConstraints> constraints;
  constraints.push_back(fr_constraints);
  constraints.push_back(gdc1_constraints);
  EXPECT_EQ(ZX_OK, controller_memory_allocator_->AllocateSharedMemory(constraints,
                                                                      &buffer_collection_info));
  EXPECT_EQ(buffer_collection_info.buffer_count, kOutputStreamMlDSMinBufferForCamping);
  EXPECT_TRUE(buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
  EXPECT_GT(buffer_collection_info.settings.buffer_settings.size_bytes,
            kOutputStreamMlFRHeight * kOutputStreamMlFRWidth);
  EXPECT_TRUE(buffer_collection_info.settings.has_image_format_constraints);
  EXPECT_EQ(fuchsia::sysmem::PixelFormatType::NV12,
            buffer_collection_info.settings.image_format_constraints.pixel_format.type);
  EXPECT_EQ(kOutputStreamMlFRHeight,
            buffer_collection_info.settings.image_format_constraints.required_max_coded_height);
  EXPECT_EQ(kOutputStreamMlFRWidth,
            buffer_collection_info.settings.image_format_constraints.required_max_coded_width);
  EXPECT_EQ(kIspBytesPerRowDivisor,
            buffer_collection_info.settings.image_format_constraints.bytes_per_row_divisor);
  for (uint32_t i = 0; i < buffer_collection_info.buffer_count; i++) {
    EXPECT_TRUE(buffer_collection_info.buffers.at(i).vmo.is_valid());
  }
  EXPECT_FALSE(
      buffer_collection_info.buffers.at(buffer_collection_info.buffer_count).vmo.is_valid());
}

// Validate FR --> GDC1
TEST_F(ControllerMemoryAllocatorTest, VideoConfigFRGDC1) {
  auto internal_config = VideoConfigFullRes(false);
  auto fr_constraints = internal_config.output_constraints;
  auto gdc1_constraints = internal_config.child_nodes[0].input_constraints;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info;
  std::vector<fuchsia::sysmem::BufferCollectionConstraints> constraints;
  constraints.push_back(fr_constraints);
  constraints.push_back(gdc1_constraints);
  EXPECT_EQ(ZX_OK, controller_memory_allocator_->AllocateSharedMemory(constraints,
                                                                      &buffer_collection_info));
  EXPECT_EQ(buffer_collection_info.buffer_count, kMlFRMinBufferForCamping + kGdcBufferForCamping);
  EXPECT_TRUE(buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
  EXPECT_GT(buffer_collection_info.settings.buffer_settings.size_bytes, kIspFRWidth * kIspFRHeight);
  EXPECT_TRUE(buffer_collection_info.settings.has_image_format_constraints);
  EXPECT_EQ(fuchsia::sysmem::PixelFormatType::NV12,
            buffer_collection_info.settings.image_format_constraints.pixel_format.type);
  EXPECT_EQ(kIspFRHeight,
            buffer_collection_info.settings.image_format_constraints.required_max_coded_height);
  EXPECT_EQ(kIspFRWidth,
            buffer_collection_info.settings.image_format_constraints.required_max_coded_width);
  EXPECT_EQ(kIspBytesPerRowDivisor,
            buffer_collection_info.settings.image_format_constraints.bytes_per_row_divisor);
  for (uint32_t i = 0; i < buffer_collection_info.buffer_count; i++) {
    EXPECT_TRUE(buffer_collection_info.buffers.at(i).vmo.is_valid());
  }
  EXPECT_FALSE(
      buffer_collection_info.buffers.at(buffer_collection_info.buffer_count).vmo.is_valid());
}

// Validate GDC1 ---> GDC2
//               |
//               ---> GE2D
TEST_F(ControllerMemoryAllocatorTest, VideoConfigGDC1GDC2) {
  auto input_node = VideoConfigFullRes(false);
  auto gdc1_node = input_node.child_nodes[0];
  auto gdc2_node = gdc1_node.child_nodes[0];
  auto ge2d_node = gdc1_node.child_nodes[1];
  auto gdc1_constraints = gdc1_node.output_constraints;
  auto gdc2_constraints = gdc2_node.input_constraints;
  auto ge2d_constraints = ge2d_node.input_constraints;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info;
  std::vector<fuchsia::sysmem::BufferCollectionConstraints> constraints;
  constraints.push_back(gdc1_constraints);
  constraints.push_back(gdc2_constraints);
  constraints.push_back(ge2d_constraints);
  EXPECT_EQ(ZX_OK, controller_memory_allocator_->AllocateSharedMemory(constraints,
                                                                      &buffer_collection_info));
  EXPECT_EQ(buffer_collection_info.buffer_count,
            kVideoMinBufferForCamping + kVideoMinBufferForCamping);
  EXPECT_TRUE(buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
  EXPECT_GT(buffer_collection_info.settings.buffer_settings.size_bytes, kGdcFRWidth * kGdcFRHeight);
  EXPECT_TRUE(buffer_collection_info.settings.has_image_format_constraints);
  EXPECT_EQ(fuchsia::sysmem::PixelFormatType::NV12,
            buffer_collection_info.settings.image_format_constraints.pixel_format.type);
  EXPECT_EQ(kGdcFRHeight,
            buffer_collection_info.settings.image_format_constraints.required_max_coded_height);
  EXPECT_EQ(kGdcFRWidth,
            buffer_collection_info.settings.image_format_constraints.required_max_coded_width);
  EXPECT_EQ(kGe2dBytesPerRowDivisor,
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
  auto internal_config = MonitorConfigDownScaledRes();
  auto ds_constraints = internal_config.output_constraints;
  auto gdc2_constraints = internal_config.child_nodes[0].input_constraints;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info;
  std::vector<fuchsia::sysmem::BufferCollectionConstraints> constraints;
  constraints.push_back(ds_constraints);
  constraints.push_back(gdc2_constraints);
  EXPECT_EQ(ZX_OK, controller_memory_allocator_->AllocateSharedMemory(constraints,
                                                                      &buffer_collection_info));
  EXPECT_EQ(buffer_collection_info.buffer_count, kOutputStreamMonitoringMinBufferForCamping);
  EXPECT_TRUE(buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
  EXPECT_GT(buffer_collection_info.settings.buffer_settings.size_bytes,
            kOutputStreamDSHeight * kOutputStreamDSWidth);
  EXPECT_TRUE(buffer_collection_info.settings.has_image_format_constraints);
  EXPECT_EQ(fuchsia::sysmem::PixelFormatType::NV12,
            buffer_collection_info.settings.image_format_constraints.pixel_format.type);
  EXPECT_EQ(0u, buffer_collection_info.settings.image_format_constraints.min_coded_height);
  EXPECT_EQ(0u, buffer_collection_info.settings.image_format_constraints.min_coded_width);
  EXPECT_EQ(kIspBytesPerRowDivisor,
            buffer_collection_info.settings.image_format_constraints.bytes_per_row_divisor);
  for (uint32_t i = 0; i < buffer_collection_info.buffer_count; i++) {
    EXPECT_TRUE(buffer_collection_info.buffers.at(i).vmo.is_valid());
  }
  EXPECT_FALSE(
      buffer_collection_info.buffers.at(buffer_collection_info.buffer_count).vmo.is_valid());
}

TEST_F(ControllerMemoryAllocatorTest, ConvertBufferCollectionInfo2TypeTest) {
  auto internal_config = MonitorConfigFullRes();
  auto fr_constraints = internal_config.output_constraints;
  auto gdc1_constraints = internal_config.child_nodes[1].input_constraints;
  fuchsia::sysmem::BufferCollectionInfo_2 hlcpp_buffer;
  std::vector<fuchsia::sysmem::BufferCollectionConstraints> constraints;
  constraints.push_back(fr_constraints);
  constraints.push_back(gdc1_constraints);
  // Allocating some buffer collection
  EXPECT_EQ(ZX_OK, controller_memory_allocator_->AllocateSharedMemory(constraints, &hlcpp_buffer));
  EXPECT_EQ(hlcpp_buffer.buffer_count, kOutputStreamMlDSMinBufferForCamping);

  BufferCollectionHelper buffer_collection_helper(hlcpp_buffer);

  fuchsia_sysmem_BufferCollectionInfo_2 c_buffer = *buffer_collection_helper.GetC();

  EXPECT_EQ(c_buffer.buffer_count, hlcpp_buffer.buffer_count);
  auto& c_buffer_settings = c_buffer.settings.buffer_settings;
  auto& hlcpp_buffer_settings = hlcpp_buffer.settings.buffer_settings;

  EXPECT_EQ(c_buffer_settings.size_bytes, hlcpp_buffer_settings.size_bytes);
  EXPECT_EQ(c_buffer_settings.is_physically_contiguous,
            hlcpp_buffer_settings.is_physically_contiguous);
  EXPECT_EQ(c_buffer_settings.is_secure, hlcpp_buffer_settings.is_secure);
  EXPECT_EQ(c_buffer_settings.coherency_domain,
            *reinterpret_cast<const fuchsia_sysmem_CoherencyDomain*>(
                &hlcpp_buffer_settings.coherency_domain));
  EXPECT_EQ(c_buffer_settings.heap,
            *reinterpret_cast<const fuchsia_sysmem_HeapType*>(&hlcpp_buffer_settings.heap));
  EXPECT_EQ(c_buffer.settings.has_image_format_constraints,
            hlcpp_buffer.settings.has_image_format_constraints);

  auto& c_image_format_constraints = c_buffer.settings.image_format_constraints;
  auto& hlcpp_image_format_constraints = hlcpp_buffer.settings.image_format_constraints;

  EXPECT_EQ(c_image_format_constraints.pixel_format.type,
            *reinterpret_cast<const fuchsia_sysmem_PixelFormatType*>(
                &hlcpp_image_format_constraints.pixel_format.type));
  EXPECT_EQ(c_image_format_constraints.pixel_format.has_format_modifier,
            hlcpp_image_format_constraints.pixel_format.has_format_modifier);
  EXPECT_EQ(c_image_format_constraints.pixel_format.format_modifier.value,
            hlcpp_image_format_constraints.pixel_format.format_modifier.value);
  EXPECT_EQ(c_image_format_constraints.color_spaces_count,
            hlcpp_image_format_constraints.color_spaces_count);

  EXPECT_EQ(c_image_format_constraints.min_coded_width,
            hlcpp_image_format_constraints.min_coded_width);
  EXPECT_EQ(c_image_format_constraints.max_coded_width,
            hlcpp_image_format_constraints.max_coded_width);
  EXPECT_EQ(c_image_format_constraints.min_coded_height,
            hlcpp_image_format_constraints.min_coded_height);
  EXPECT_EQ(c_image_format_constraints.max_coded_height,
            hlcpp_image_format_constraints.max_coded_height);
  EXPECT_EQ(c_image_format_constraints.min_bytes_per_row,
            hlcpp_image_format_constraints.min_bytes_per_row);
  EXPECT_EQ(c_image_format_constraints.max_bytes_per_row,
            hlcpp_image_format_constraints.max_bytes_per_row);

  EXPECT_EQ(c_image_format_constraints.max_coded_width_times_coded_height,
            hlcpp_image_format_constraints.max_coded_width_times_coded_height);
  EXPECT_EQ(c_image_format_constraints.layers, hlcpp_image_format_constraints.layers);
  EXPECT_EQ(c_image_format_constraints.coded_width_divisor,
            hlcpp_image_format_constraints.coded_width_divisor);
  EXPECT_EQ(c_image_format_constraints.coded_height_divisor,
            hlcpp_image_format_constraints.coded_height_divisor);
  EXPECT_EQ(c_image_format_constraints.bytes_per_row_divisor,
            hlcpp_image_format_constraints.bytes_per_row_divisor);
  EXPECT_EQ(c_image_format_constraints.start_offset_divisor,
            hlcpp_image_format_constraints.start_offset_divisor);
  EXPECT_EQ(c_image_format_constraints.display_width_divisor,
            hlcpp_image_format_constraints.display_width_divisor);
  EXPECT_EQ(c_image_format_constraints.display_height_divisor,
            hlcpp_image_format_constraints.display_height_divisor);
  EXPECT_EQ(c_image_format_constraints.required_min_coded_width,
            hlcpp_image_format_constraints.required_min_coded_width);
  EXPECT_EQ(c_image_format_constraints.required_max_coded_width,
            hlcpp_image_format_constraints.required_max_coded_width);
  EXPECT_EQ(c_image_format_constraints.required_min_coded_height,
            hlcpp_image_format_constraints.required_min_coded_height);
  EXPECT_EQ(c_image_format_constraints.required_max_coded_height,
            hlcpp_image_format_constraints.required_max_coded_height);
  EXPECT_EQ(c_image_format_constraints.required_min_bytes_per_row,
            hlcpp_image_format_constraints.required_min_bytes_per_row);
  EXPECT_EQ(c_image_format_constraints.required_max_bytes_per_row,
            hlcpp_image_format_constraints.required_max_bytes_per_row);

  for (uint32_t i = 0; i < c_buffer.buffer_count; ++i) {
    EXPECT_EQ(ZX_OK, zx_handle_close(c_buffer.buffers[i].vmo));
  }
}

TEST_F(ControllerMemoryAllocatorTest, ConvertImageFormat2TypeTest) {
  auto internal_config = MonitorConfigFullRes();
  auto vector_image_formats = internal_config.image_formats;
  fuchsia::sysmem::ImageFormat_2 hlcpp_image_format = vector_image_formats[0];

  fuchsia_sysmem_ImageFormat_2 c_image_format = ConvertHlcppImageFormat2toCType(hlcpp_image_format);

  EXPECT_EQ(c_image_format.pixel_format.type,
            *reinterpret_cast<const fuchsia_sysmem_PixelFormatType*>(
                &hlcpp_image_format.pixel_format.type));
  EXPECT_EQ(c_image_format.pixel_format.has_format_modifier,
            hlcpp_image_format.pixel_format.has_format_modifier);
  EXPECT_EQ(c_image_format.pixel_format.format_modifier.value,
            hlcpp_image_format.pixel_format.format_modifier.value);

  EXPECT_EQ(c_image_format.coded_width, hlcpp_image_format.coded_width);
  EXPECT_EQ(c_image_format.coded_height, hlcpp_image_format.coded_height);
  EXPECT_EQ(c_image_format.bytes_per_row, hlcpp_image_format.bytes_per_row);
  EXPECT_EQ(c_image_format.display_width, hlcpp_image_format.display_width);
  EXPECT_EQ(c_image_format.display_height, hlcpp_image_format.display_height);
  EXPECT_EQ(c_image_format.layers, hlcpp_image_format.layers);
  EXPECT_EQ(c_image_format.has_pixel_aspect_ratio, hlcpp_image_format.has_pixel_aspect_ratio);
  EXPECT_EQ(c_image_format.pixel_aspect_ratio_width, hlcpp_image_format.pixel_aspect_ratio_width);
  EXPECT_EQ(c_image_format.pixel_aspect_ratio_height, hlcpp_image_format.pixel_aspect_ratio_height);
}

}  // namespace camera
