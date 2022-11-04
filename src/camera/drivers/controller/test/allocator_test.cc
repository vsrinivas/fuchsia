// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/sys/cpp/component_context.h>

#include "src/camera/drivers/controller/memory_allocation.h"
#include "src/camera/drivers/controller/pipeline_manager.h"
#include "src/camera/drivers/controller/sherlock/common_util.h"
#include "src/camera/drivers/controller/sherlock/monitoring_config.h"
#include "src/camera/drivers/controller/sherlock/video_conferencing_config.h"
#include "src/camera/drivers/controller/test/fake_sysmem.h"
#include "src/camera/lib/format_conversion/format_conversion.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

// Relaxes memory placement constraints to be more permissive. This improves the reliability of
// allocation as it is no longer competing with other components in the system for fixed
// allocations of e.g. contiguous memory.
void RelaxMemoryConstraints(
    std::vector<fuchsia::sysmem::BufferCollectionConstraints>& constraints) {
  for (auto& c : constraints) {
    c.buffer_memory_constraints.cpu_domain_supported = true;
    c.buffer_memory_constraints.ram_domain_supported = true;
    c.buffer_memory_constraints.inaccessible_domain_supported = true;
    c.buffer_memory_constraints.secure_required = false;
    c.buffer_memory_constraints.physically_contiguous_required = false;
  }
}

// NOTE: In this test, we are actually just unit testing
// the sysmem allocation using different constraints.
namespace camera {

class ControllerMemoryAllocatorTest : public gtest::TestLoopFixture {
 public:
  ControllerMemoryAllocatorTest()
      : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
        fake_sysmem_([this](auto request) { return context_->svc()->Connect(std::move(request)); }),
        sysmem_(fake_sysmem_.client()) {}

  void SetUp() override {
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(sysmem_allocator_.NewRequest()));
    ASSERT_EQ(ZX_OK, zx::event::create(0, &event_));

    controller_memory_allocator_ = std::make_unique<ControllerMemoryAllocator>(sysmem_);
    pipeline_manager_ = std::make_unique<PipelineManager>(
        dispatcher(), sysmem_, isp_, gdc_, ge2d_,
        fit::bind_member(this, &ControllerMemoryAllocatorTest::LoadFirmware));
  }

  void TearDown() override {
    context_ = nullptr;
    sysmem_allocator_ = nullptr;
  }

  fpromise::result<std::pair<zx::vmo, size_t>, zx_status_t> LoadFirmware(const std::string& path) {
    constexpr size_t kVmoSize = 4096;
    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(kVmoSize, 0, &vmo);
    if (status != ZX_OK) {
      return fpromise::error(status);
    }
    return fpromise::ok(std::pair{std::move(vmo), kVmoSize});
  }

  zx::event event_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  std::unique_ptr<ControllerMemoryAllocator> controller_memory_allocator_;
  std::unique_ptr<camera::PipelineManager> pipeline_manager_;
  FakeSysmem fake_sysmem_;
  ddk::SysmemProtocolClient sysmem_;
  ddk::IspProtocolClient isp_;
  ddk::GdcProtocolClient gdc_;
  ddk::Ge2dProtocolClient ge2d_;
};

// Validate FR --> GDC1 --> OutputStreamMLDS
// Buffer collection constraints.
TEST_F(ControllerMemoryAllocatorTest, MonitorConfigFR) {
  auto internal_config = MonitorConfigFullRes();
  auto fr_constraints = *internal_config.output_constraints;
  auto gdc1_constraints = *internal_config.child_nodes[0].child_nodes[1].input_constraints;
  BufferCollection buffer_collection;
  std::vector<fuchsia::sysmem::BufferCollectionConstraints> constraints;
  constraints.push_back(fr_constraints);
  constraints.push_back(gdc1_constraints);
  RelaxMemoryConstraints(constraints);
  ASSERT_EQ(ZX_OK, controller_memory_allocator_->AllocateSharedMemory(
                       constraints, buffer_collection, "TestMonitorConfigFR"));
  EXPECT_GT(buffer_collection.buffers.settings.buffer_settings.size_bytes,
            kOutputStreamMlFRHeight * kOutputStreamMlFRWidth);
  EXPECT_TRUE(buffer_collection.buffers.settings.has_image_format_constraints);
  EXPECT_EQ(fuchsia::sysmem::PixelFormatType::NV12,
            buffer_collection.buffers.settings.image_format_constraints.pixel_format.type);
  EXPECT_EQ(kOutputStreamMlFRHeight,
            buffer_collection.buffers.settings.image_format_constraints.required_max_coded_height);
  EXPECT_EQ(kOutputStreamMlFRWidth,
            buffer_collection.buffers.settings.image_format_constraints.required_max_coded_width);
  EXPECT_EQ(kIspBytesPerRowDivisor,
            buffer_collection.buffers.settings.image_format_constraints.bytes_per_row_divisor);
  for (uint32_t i = 0; i < buffer_collection.buffers.buffer_count; i++) {
    EXPECT_TRUE(buffer_collection.buffers.buffers.at(i).vmo.is_valid());
  }
  EXPECT_FALSE(
      buffer_collection.buffers.buffers.at(buffer_collection.buffers.buffer_count).vmo.is_valid());
}

// Validate FR --> GDC1
TEST_F(ControllerMemoryAllocatorTest, VideoConfigFRGDC1) {
  auto internal_config = VideoConfigFullRes(false);
  auto fr_constraints = *internal_config.output_constraints;
  auto gdc1_constraints = *internal_config.child_nodes[0].input_constraints;
  BufferCollection buffer_collection;
  std::vector<fuchsia::sysmem::BufferCollectionConstraints> constraints;
  constraints.push_back(fr_constraints);
  constraints.push_back(gdc1_constraints);
  RelaxMemoryConstraints(constraints);
  ASSERT_EQ(ZX_OK, controller_memory_allocator_->AllocateSharedMemory(
                       constraints, buffer_collection, "TestVideoConfigFRGDC1"));
  EXPECT_GT(buffer_collection.buffers.settings.buffer_settings.size_bytes,
            kIspFRWidth * kIspFRHeight);
  EXPECT_TRUE(buffer_collection.buffers.settings.has_image_format_constraints);
  EXPECT_EQ(fuchsia::sysmem::PixelFormatType::NV12,
            buffer_collection.buffers.settings.image_format_constraints.pixel_format.type);
  EXPECT_EQ(kIspFRHeight,
            buffer_collection.buffers.settings.image_format_constraints.required_max_coded_height);
  EXPECT_EQ(kIspFRWidth,
            buffer_collection.buffers.settings.image_format_constraints.required_max_coded_width);
  EXPECT_EQ(kIspBytesPerRowDivisor,
            buffer_collection.buffers.settings.image_format_constraints.bytes_per_row_divisor);
  for (uint32_t i = 0; i < buffer_collection.buffers.buffer_count; i++) {
    EXPECT_TRUE(buffer_collection.buffers.buffers.at(i).vmo.is_valid());
  }
  EXPECT_FALSE(
      buffer_collection.buffers.buffers.at(buffer_collection.buffers.buffer_count).vmo.is_valid());
}

// Validate GDC1 ---> GDC2
//               |
//               ---> GE2D
TEST_F(ControllerMemoryAllocatorTest, VideoConfigGDC1GDC2) {
  auto input_node = VideoConfigFullRes(false);
  auto gdc1_node = input_node.child_nodes[0];
  auto gdc2_node = gdc1_node.child_nodes[0];
  auto ge2d_node = gdc1_node.child_nodes[1];
  auto gdc1_constraints = *gdc1_node.output_constraints;
  auto gdc2_constraints = *gdc2_node.input_constraints;
  auto ge2d_constraints = *ge2d_node.input_constraints;
  BufferCollection buffer_collection;
  std::vector<fuchsia::sysmem::BufferCollectionConstraints> constraints;
  constraints.push_back(gdc1_constraints);
  constraints.push_back(gdc2_constraints);
  constraints.push_back(ge2d_constraints);
  RelaxMemoryConstraints(constraints);
  ASSERT_EQ(ZX_OK, controller_memory_allocator_->AllocateSharedMemory(
                       constraints, buffer_collection, "TestVideoConfigGDC1GDC2"));
  EXPECT_GT(buffer_collection.buffers.settings.buffer_settings.size_bytes,
            kGdcFRWidth * kGdcFRHeight);
  EXPECT_TRUE(buffer_collection.buffers.settings.has_image_format_constraints);
  EXPECT_EQ(fuchsia::sysmem::PixelFormatType::NV12,
            buffer_collection.buffers.settings.image_format_constraints.pixel_format.type);
  EXPECT_EQ(kGdcFRHeight,
            buffer_collection.buffers.settings.image_format_constraints.required_max_coded_height);
  EXPECT_EQ(kGdcFRWidth,
            buffer_collection.buffers.settings.image_format_constraints.required_max_coded_width);
  EXPECT_EQ(kGe2dBytesPerRowDivisor,
            buffer_collection.buffers.settings.image_format_constraints.bytes_per_row_divisor);
  for (uint32_t i = 0; i < buffer_collection.buffers.buffer_count; i++) {
    EXPECT_TRUE(buffer_collection.buffers.buffers.at(i).vmo.is_valid());
  }
  EXPECT_FALSE(
      buffer_collection.buffers.buffers.at(buffer_collection.buffers.buffer_count).vmo.is_valid());
}

// Validate DS --> GDC2 --> (GE2D) --> OutputStreamMonitoring
// This validates only DS --> GDC2
// Buffer collection constraints.
TEST_F(ControllerMemoryAllocatorTest, MonitorConfigDS) {
  auto internal_config = MonitorConfigDownScaledRes();
  auto ds_constraints = *internal_config.output_constraints;
  auto gdc2_constraints = *internal_config.child_nodes[0].input_constraints;
  BufferCollection buffer_collection;
  std::vector<fuchsia::sysmem::BufferCollectionConstraints> constraints;
  constraints.push_back(ds_constraints);
  constraints.push_back(gdc2_constraints);
  RelaxMemoryConstraints(constraints);
  ASSERT_EQ(ZX_OK, controller_memory_allocator_->AllocateSharedMemory(
                       constraints, buffer_collection, "TestMonitorConfigDS"));
  EXPECT_GT(buffer_collection.buffers.settings.buffer_settings.size_bytes,
            kOutputStreamDSHeight * kOutputStreamDSWidth);
  EXPECT_TRUE(buffer_collection.buffers.settings.has_image_format_constraints);
  EXPECT_EQ(fuchsia::sysmem::PixelFormatType::NV12,
            buffer_collection.buffers.settings.image_format_constraints.pixel_format.type);
  EXPECT_EQ(kIspBytesPerRowDivisor,
            buffer_collection.buffers.settings.image_format_constraints.bytes_per_row_divisor);
  for (uint32_t i = 0; i < buffer_collection.buffers.buffer_count; i++) {
    EXPECT_TRUE(buffer_collection.buffers.buffers.at(i).vmo.is_valid());
  }
  EXPECT_FALSE(
      buffer_collection.buffers.buffers.at(buffer_collection.buffers.buffer_count).vmo.is_valid());
}

TEST_F(ControllerMemoryAllocatorTest, ConvertImageFormat2TypeTest) {
  auto internal_config = MonitorConfigFullRes();
  auto vector_image_formats = internal_config.image_formats;
  fuchsia::sysmem::ImageFormat_2 hlcpp_image_format = vector_image_formats[0];

  fuchsia_sysmem::wire::ImageFormat2 c_image_format = ConvertToWireType(hlcpp_image_format);

  EXPECT_EQ(c_image_format.pixel_format.type,
            static_cast<const fuchsia_sysmem::wire::PixelFormatType>(
                hlcpp_image_format.pixel_format.type));
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
