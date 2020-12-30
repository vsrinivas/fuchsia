// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon-internal/align.h>

#include "src/ui/lib/display/get_hardware_display_controller.h"
#include "src/ui/lib/escher/flatland/rectangle_compositor.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/batch_gpu_downloader.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/display/util.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "src/ui/scenic/lib/flatland/engine/tests/common.h"
#include "src/ui/scenic/lib/flatland/renderer/gpu_mem.h"

using ::testing::_;
using ::testing::Return;

using flatland::ImageMetadata;
using flatland::LinkSystem;
using flatland::Renderer;
using flatland::TransformGraph;
using flatland::TransformHandle;
using flatland::UberStruct;
using flatland::UberStructSystem;
using fuchsia::ui::scenic::internal::ContentLink;
using fuchsia::ui::scenic::internal::ContentLinkStatus;
using fuchsia::ui::scenic::internal::ContentLinkToken;
using fuchsia::ui::scenic::internal::GraphLink;
using fuchsia::ui::scenic::internal::GraphLinkToken;
using fuchsia::ui::scenic::internal::LayoutInfo;
using fuchsia::ui::scenic::internal::LinkProperties;

using namespace scenic_impl;
using namespace display;

namespace flatland {
namespace test {

class EnginePixelTest : public EngineTestBase {
 public:
  void SetUp() override {
    EngineTestBase::SetUp();

    // Create the SysmemAllocator.
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator_.NewRequest().TakeChannel().release());
    EXPECT_EQ(status, ZX_OK);

    executor_ = std::make_unique<async::Executor>(dispatcher());

    display_manager_ = std::make_unique<display::DisplayManager>([]() {});

    auto hdc_promise = ui_display::GetHardwareDisplayController();
    executor_->schedule_task(
        hdc_promise.then([this](fit::result<ui_display::DisplayControllerHandles>& handles) {
          display_manager_->BindDefaultDisplayController(std::move(handles.value().controller),
                                                         std::move(handles.value().dc_device));
        }));

    RunLoopUntil([this] { return display_manager_->default_display() != nullptr; });

    // By using the null renderer, we can demonstrate that the rendering is being done directly
    // by the display controller hardware, and not the software renderer.
    renderer_ = std::make_shared<flatland::NullRenderer>();

    engine_ = std::make_unique<flatland::Engine>(display_manager_->default_display_controller(),
                                                 renderer_, link_system(), uber_struct_system());
  }

  void TearDown() override {
    renderer_.reset();
    engine_.reset();

    EngineTestBase::TearDown();
  }

 protected:
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  std::unique_ptr<async::Executor> executor_;
  std::unique_ptr<display::DisplayManager> display_manager_;
  std::unique_ptr<flatland::Engine> engine_;
  std::shared_ptr<flatland::Renderer> renderer_;

  // Set up the buffer collections and images to be used for capturing the diplay controller's
  // output. The only devices which currently implement the capture functionality on their
  // display controllers are the AMLOGIC devices, and so we hardcode some of those AMLOGIC
  // assumptions here, such as making the pixel format for the capture image BGR24, as that
  // is the only capture format that AMLOGIC supports.
  fuchsia::sysmem::BufferCollectionSyncPtr SetupCapture(
      sysmem_util::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::BufferCollectionInfo_2* collection_info, uint64_t* image_id) {
    auto display = display_manager_->default_display();
    auto display_controller = display_manager_->default_display_controller();
    EXPECT_TRUE(display);
    EXPECT_TRUE(display_controller);

    // This should only be running on devices with capture support.
    bool capture_supported = scenic_impl::IsCaptureSupported(*display_controller.get());
    EXPECT_TRUE(capture_supported);

    // Set up buffer collection and image for recording a snapshot.
    fuchsia::hardware::display::ImageConfig image_config = {
        .type = fuchsia::hardware::display::TYPE_CAPTURE};

    auto tokens = SysmemTokens::Create(sysmem_allocator_.get());
    auto result = scenic_impl::ImportBufferCollection(collection_id, *display_controller.get(),
                                                      std::move(tokens.dup_token), image_config);
    EXPECT_TRUE(result);
    fuchsia::sysmem::BufferCollectionSyncPtr collection;
    zx_status_t status = sysmem_allocator_->BindSharedCollection(std::move(tokens.local_token),
                                                                 collection.NewRequest());
    EXPECT_EQ(status, ZX_OK);

    // Set the client constraints.
    {
      fuchsia::sysmem::BufferCollectionConstraints constraints;

      // finally setup our constraints
      constraints.usage.cpu =
          fuchsia::sysmem::cpuUsageReadOften | fuchsia::sysmem::cpuUsageWriteOften;
      constraints.min_buffer_count_for_camping = 1;
      constraints.has_buffer_memory_constraints = true;
      constraints.buffer_memory_constraints.ram_domain_supported = true;
      constraints.image_format_constraints_count = 1;
      fuchsia::sysmem::ImageFormatConstraints& image_constraints =
          constraints.image_format_constraints[0];

      // Compatible with ZX_PIXEL_FORMAT_RGB_888. This format required for AMLOGIC capture.
      image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGR24;

      image_constraints.color_spaces_count = 1;
      image_constraints.color_space[0] = fuchsia::sysmem::ColorSpace{
          .type = fuchsia::sysmem::ColorSpaceType::SRGB,
      };
      image_constraints.min_coded_width = 0;
      image_constraints.max_coded_width = std::numeric_limits<uint32_t>::max();
      image_constraints.min_coded_height = 0;
      image_constraints.max_coded_height = std::numeric_limits<uint32_t>::max();
      image_constraints.min_bytes_per_row = 0;
      image_constraints.max_bytes_per_row = std::numeric_limits<uint32_t>::max();
      image_constraints.max_coded_width_times_coded_height = std::numeric_limits<uint32_t>::max();
      image_constraints.layers = 1;
      image_constraints.coded_width_divisor = 1;
      image_constraints.coded_height_divisor = 1;
      image_constraints.bytes_per_row_divisor = 1;
      image_constraints.start_offset_divisor = 1;
      image_constraints.display_width_divisor = 1;
      image_constraints.display_height_divisor = 1;

      status = collection->SetConstraints(true /* has_constraints */, constraints);
      EXPECT_EQ(status, ZX_OK);
    }

    // Have the client wait for buffers allocated so it can populate its information
    // struct with the vmo data.
    {
      zx_status_t allocation_status = ZX_OK;
      status = collection->WaitForBuffersAllocated(&allocation_status, collection_info);
      EXPECT_EQ(status, ZX_OK);
      EXPECT_EQ(allocation_status, ZX_OK);
    }

    *image_id = scenic_impl::ImportImageForCapture(*display_controller.get(), image_config,
                                                   collection_id, 0);

    return collection;
  }

  // Sets up the buffer collection information for collections that will be imported
  // into the engine.
  fuchsia::sysmem::BufferCollectionSyncPtr SetupTextures(
      sysmem_util::GlobalBufferCollectionId collection_id, uint32_t width, uint32_t height,
      uint32_t num_vmos, fuchsia::sysmem::BufferCollectionInfo_2* collection_info) {
    // Setup the buffer collection that will be used for the flatland rectangle's texture.
    auto texture_tokens = SysmemTokens::Create(sysmem_allocator_.get());
    auto result = engine_->ImportBufferCollection(collection_id, sysmem_allocator_.get(),
                                                  std::move(texture_tokens.dup_token));
    EXPECT_TRUE(result);
    auto [buffer_usage, memory_constraints] = GetUsageAndMemoryConstraintsForCpuWriteOften();
    fuchsia::sysmem::BufferCollectionSyncPtr texture_collection =
        CreateClientPointerWithConstraints(sysmem_allocator_.get(),
                                           std::move(texture_tokens.local_token), num_vmos, width,
                                           height, buffer_usage, memory_constraints);

    // Have the client wait for buffers allocated so it can populate its information
    // struct with the vmo data.
    {
      zx_status_t allocation_status = ZX_OK;
      auto status =
          texture_collection->WaitForBuffersAllocated(&allocation_status, collection_info);
      EXPECT_EQ(status, ZX_OK);
      EXPECT_EQ(allocation_status, ZX_OK);
    }

    return texture_collection;
  }

  // Captures the pixel values on the display and reads them into |read_values|.
  void CaptureDisplayOutput(const fuchsia::sysmem::BufferCollectionInfo_2& collection_info,
                            uint64_t capture_image_id, std::vector<uint8_t>* read_values) {
    // This ID would only be zero if we were running in an environment without capture support.
    EXPECT_NE(capture_image_id, 0U);

    auto display = display_manager_->default_display();
    auto display_controller = display_manager_->default_display_controller();

    zx::event capture_signal_fence;
    auto status = zx::event::create(0, &capture_signal_fence);
    EXPECT_EQ(status, ZX_OK);

    auto capture_signal_fence_id =
        scenic_impl::ImportEvent(*display_controller.get(), capture_signal_fence);
    fuchsia::hardware::display::Controller_StartCapture_Result start_capture_result;
    (*display_controller.get())
        ->StartCapture(capture_signal_fence_id, capture_image_id, &start_capture_result);
    EXPECT_TRUE(start_capture_result.is_response());

    // We must wait for the capture to finish before we can proceed. Time out after 3 seconds.
    status = capture_signal_fence.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::msec(3000)),
                                           nullptr);
    EXPECT_EQ(status, ZX_OK);

    // Read the capture values back out.
    MapHostPointer(collection_info, /*vmo_idx*/ 0,
                   [read_values](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                     read_values->resize(num_bytes);
                     memcpy(read_values->data(), vmo_host, num_bytes);
                   });

    // Cleanup the capture.
    fuchsia::hardware::display::Controller_ReleaseCapture_Result result_capture_result;
    (*display_controller.get())->ReleaseCapture(capture_image_id, &result_capture_result);
    EXPECT_TRUE(result_capture_result.is_response());
  }

  // This function is taken directly from the zircon display capture test and modified slightly
  // to fit this test.
  bool AmlogicCaptureCompare(void* capture_buf, void* actual_buf, size_t size, uint32_t height,
                             uint32_t width) {
    auto image_buf = std::make_unique<uint8_t[]>(size);
    std::memcpy(image_buf.get(), actual_buf, size);

    auto* imageptr = static_cast<uint8_t*>(image_buf.get());
    auto* captureptr = static_cast<uint8_t*>(capture_buf);

    // first fix endianess
    auto* tmpptr = reinterpret_cast<uint32_t*>(image_buf.get());
    for (size_t i = 0; i < size / 4; i++) {
      tmpptr[i] = be32toh(tmpptr[i]);
    }

    uint32_t capture_stride = ZX_ALIGN(width * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_888), 64);
    uint32_t buffer_stride = ZX_ALIGN(width * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_x888), 64);
    uint32_t buffer_width_bytes = width * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_x888);
    uint32_t capture_width_bytes = width * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_888);
    size_t buf_idx = 0;

    // For Astro only:
    // Ignore last column. Has junk (hardware bug)
    // Ignoring last column, means there is a shift by one pixel.
    // Therefore, image_buffer should start from pixel 1 (i.e. 4th byte since x888) and
    // capture_buffer should end at width - 3 (i.e. 888)
    capture_width_bytes -= ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_888);
    buf_idx = ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_x888);

    size_t cap_idx = 0;
    // Ignore first line. It <sometimes> contains junk (hardware bug).
    bool success = true;
    for (size_t h = 1; h < height; h++) {
      for (; cap_idx < capture_width_bytes && buf_idx < buffer_width_bytes;) {
        // skip the alpha channel
        if (((buf_idx) % 4) == 0) {
          buf_idx++;
          continue;
        }
        if (imageptr[h * buffer_stride + buf_idx] == captureptr[h * capture_stride + cap_idx]) {
          buf_idx++;
          cap_idx++;
          continue;
        }
        if (imageptr[h * buffer_stride + buf_idx] != 0 &&
            (imageptr[h * buffer_stride + buf_idx] ==
                 captureptr[h * capture_stride + cap_idx] + 1 ||
             imageptr[h * buffer_stride + buf_idx] ==
                 captureptr[h * capture_stride + cap_idx] - 1)) {
          buf_idx++;
          cap_idx++;
          continue;
        }
        success = false;
        break;
      }
      if (!success) {
        break;
      }
    }
    return success;
  }
};

// Renders a fullscreen green rectangle to the provided display. This
// tests the engine's ability to properly read in flatland uberstruct
// data and then pass the data along to the display-controller interface
// to be composited directly in hardware. The Astro display controller
// only handles full screen rects.
TEST_F(EnginePixelTest, FullscreenRectangleTest) {
  auto display = display_manager_->default_display();
  auto display_controller = display_manager_->default_display_controller();

  const uint64_t kTextureCollectionId = sysmem_util::GenerateUniqueBufferCollectionId();
  const uint64_t kCaptureCollectionId = sysmem_util::GenerateUniqueBufferCollectionId();

  // Set up buffer collection and image for display_controller capture.
  uint64_t capture_image_id;
  fuchsia::sysmem::BufferCollectionInfo_2 capture_info;
  auto capture_collection = SetupCapture(kCaptureCollectionId, &capture_info, &capture_image_id);

  // Setup the collection for the texture. Due to display controller limitations, the size of
  // the texture needs to match the size of the rect. So since we have a fullscreen rect, we
  // must also have a fullscreen texture to match.
  const uint32_t kRectWidth = display->width_in_px(), kTextureWidth = display->width_in_px();
  const uint32_t kRectHeight = display->height_in_px(), kTextureHeight = display->height_in_px();
  fuchsia::sysmem::BufferCollectionInfo_2 texture_collection_info;
  auto texture_collection = SetupTextures(kTextureCollectionId, kTextureWidth, kTextureHeight, 1,
                                          &texture_collection_info);

  // Get a raw pointer for the texture's vmo and make it green. DC uses ARGB format.
  uint32_t col = (255U << 24) | (255U << 8);
  std::vector<uint32_t> write_values;
  write_values.assign(kTextureWidth * kTextureHeight, col);
  MapHostPointer(texture_collection_info, /*vmo_idx*/ 0,
                 [write_values](uint8_t* vmo_host, uint32_t num_bytes) {
                   EXPECT_TRUE(num_bytes >= sizeof(uint32_t) * write_values.size());
                   memcpy(vmo_host, write_values.data(), sizeof(uint32_t) * write_values.size());
                 });

  // Import the texture to the engine.
  auto image_metadata = ImageMetadata{.collection_id = kTextureCollectionId,
                                      .identifier = 1,
                                      .vmo_idx = 0,
                                      .width = kTextureWidth,
                                      .height = kTextureHeight};
  auto result = engine_->ImportImage(image_metadata);
  EXPECT_TRUE(result);

  // Create a flatland session with a root and image handle. Import to the engine as display root.
  auto session = CreateSession();
  const TransformHandle root_handle = session.graph().CreateTransform();
  const TransformHandle image_handle = session.graph().CreateTransform();
  session.graph().AddChild(root_handle, image_handle);
  engine_->AddDisplay(display->display_id(), root_handle,
                      glm::uvec2(display->width_in_px(), display->height_in_px()));

  // Setup the uberstruct data.
  auto uberstruct = session.CreateUberStructWithCurrentTopology(root_handle);
  uberstruct->images[image_handle] = image_metadata;
  uberstruct->local_matrices[image_handle] = glm::scale(
      glm::translate(glm::mat3(1.0), glm::vec2(0, 0)), glm::vec2(kRectWidth, kRectHeight));
  session.PushUberStruct(std::move(uberstruct));

  // Now we can finally render.
  engine_->RenderFrame();

  // Grab the capture vmo data.
  std::vector<uint8_t> read_values;
  CaptureDisplayOutput(capture_info, capture_image_id, &read_values);

  // Compare the capture vmo data to the texture data above. Since we're doing a full screen
  // render, the two should be identical. The comparison is a bit complicated though since
  // the images are of two different formats.
  bool images_are_same =
      AmlogicCaptureCompare(read_values.data(), write_values.data(), read_values.size(),
                            display->height_in_px(), display->width_in_px());
  EXPECT_TRUE(images_are_same);
}

}  // namespace test
}  // namespace flatland
