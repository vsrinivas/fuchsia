// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon-internal/align.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/lib/display/get_hardware_display_controller.h"
#include "src/ui/lib/escher/flatland/rectangle_compositor.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/batch_gpu_downloader.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/display/util.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "src/ui/scenic/lib/flatland/engine/tests/common.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"
#include "src/ui/scenic/lib/utils/helpers.h"

using ::testing::_;
using ::testing::Return;

using allocation::BufferCollectionUsage;
using allocation::ImageMetadata;
using flatland::LinkSystem;
using flatland::Renderer;
using flatland::TransformGraph;
using flatland::TransformHandle;
using flatland::UberStruct;
using flatland::UberStructSystem;
using fuchsia::ui::composition::ChildViewStatus;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::LayoutInfo;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::composition::ViewportProperties;
using fuchsia::ui::views::ViewCreationToken;
using fuchsia::ui::views::ViewportCreationToken;

using namespace scenic_impl;
using namespace display;

namespace flatland {
namespace test {

class DisplayCompositorPixelTest : public DisplayCompositorTestBase {
 public:
  void SetUp() override {
    DisplayCompositorTestBase::SetUp();

    // Create the SysmemAllocator.
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator_.NewRequest().TakeChannel().release());
    EXPECT_EQ(status, ZX_OK);
    sysmem_allocator_->SetDebugClientInfo(fsl::GetCurrentProcessName(),
                                          fsl::GetCurrentProcessKoid());

    executor_ = std::make_unique<async::Executor>(dispatcher());

    display_manager_ = std::make_unique<display::DisplayManager>([]() {});

    auto hdc_promise = ui_display::GetHardwareDisplayController();
    executor_->schedule_task(
        hdc_promise.then([this](fpromise::result<ui_display::DisplayControllerHandles>& handles) {
          display_manager_->BindDefaultDisplayController(std::move(handles.value().controller),
                                                         std::move(handles.value().dc_device));
        }));

    RunLoopUntil([this] { return display_manager_->default_display() != nullptr; });

    // Enable Vsync so that vsync events will be given to this client.
    auto display_controller = display_manager_->default_display_controller();
    (*display_controller.get())->EnableVsync(true);
  }

  void TearDown() override {
    RunLoopUntilIdle();
    executor_.reset();
    display_manager_.reset();
    DisplayCompositorTestBase::TearDown();
  }

  bool IsDisplaySupported(DisplayCompositor* display_compositor,
                          allocation::GlobalBufferCollectionId id) {
    return display_compositor->buffer_collection_supports_display_[id];
  }

 protected:
  const zx_pixel_format_t kPixelFormat = ZX_PIXEL_FORMAT_ARGB_8888;

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  std::unique_ptr<async::Executor> executor_;
  std::unique_ptr<display::DisplayManager> display_manager_;

  static std::pair<std::unique_ptr<escher::Escher>, std::shared_ptr<flatland::VkRenderer>>
  NewVkRenderer() {
    auto env = escher::test::EscherEnvironment::GetGlobalTestEnvironment();
    auto unique_escher = std::make_unique<escher::Escher>(
        env->GetVulkanDevice(), env->GetFilesystem(), /*gpu_allocator*/ nullptr);
    return {std::move(unique_escher),
            std::make_shared<flatland::VkRenderer>(unique_escher->GetWeakPtr())};
  }

  static std::shared_ptr<flatland::NullRenderer> NewNullRenderer() {
    return std::make_shared<flatland::NullRenderer>();
  }

  // To avoid flakes, tests call this function to ensure that config stamps applied by
  // the display compositor are fully applied to the display controller before engaging
  // in any operations (e.g. reading back pixels from the display) that first require
  // these processes to have been completed.
  void WaitOnVSync() {
    auto display = display_manager_->default_display();
    auto display_controller = display_manager_->default_display_controller();

    // Get the latest applied config stamp. This will be used to compare against the config
    // stamp in the OnSync callback function used by the display. If the two stamps match,
    // then we know that the vsync has completed and it is safe to do readbacks.
    fuchsia::hardware::display::ConfigStamp pending_config_stamp;
    auto status = (*display_controller.get())->GetLatestAppliedConfigStamp(&pending_config_stamp);
    ASSERT_TRUE(status == ZX_OK);

    // The callback will switch this bool to |true| if the two configs match. It is initialized
    // to |false| and blocks the main thread below.
    bool configs_are_equal = false;
    display->SetVsyncCallback([&pending_config_stamp, &configs_are_equal](
                                  zx::time timestamp,
                                  fuchsia::hardware::display::ConfigStamp applied_config_stamp) {
      if (pending_config_stamp.value == applied_config_stamp.value &&
          applied_config_stamp.value != fuchsia::hardware::display::INVALID_CONFIG_STAMP_VALUE) {
        configs_are_equal = true;
      }
    });

    // Run loop until the configs match.
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&configs_are_equal] { return configs_are_equal; },
                                          /*timeout*/ zx::sec(10)));

    // Now that we've finished waiting, we can reset the display callback to null as we do not want
    // this callback, which makes references to stack variables which will go out of scope once this
    // function exits, to continue being called on future vsyncs.
    display->SetVsyncCallback(nullptr);
  }

  // Set up the buffer collections and images to be used for capturing the diplay controller's
  // output. The only devices which currently implement the capture functionality on their
  // display controllers are the AMLOGIC devices, and so we hardcode some of those AMLOGIC
  // assumptions here, such as making the pixel format for the capture image BGR24, as that
  // is the only capture format that AMLOGIC supports.
  fpromise::result<fuchsia::sysmem::BufferCollectionSyncPtr, zx_status_t> SetupCapture(
      allocation::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::PixelFormatType pixel_type,
      fuchsia::sysmem::BufferCollectionInfo_2* collection_info, uint64_t* image_id) {
    auto display = display_manager_->default_display();
    auto display_controller = display_manager_->default_display_controller();
    EXPECT_TRUE(display);
    EXPECT_TRUE(display_controller);

    // This should only be running on devices with capture support.
    bool capture_supported = scenic_impl::IsCaptureSupported(*display_controller.get());
    if (!capture_supported) {
      FX_LOGS(WARNING) << "Capture is not supported on this device. Test skipped.";
      return fpromise::error(ZX_ERR_NOT_SUPPORTED);
    }

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

    collection->SetName(100u, "FlatlandTestCaptureImage");

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

#ifdef FAKE_DISPLAY
      image_constraints.pixel_format.type = pixel_type;
#else
      // Compatible with ZX_PIXEL_FORMAT_RGB_888. This format required for AMLOGIC capture.
      image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGR24;
#endif  // FAKE_DISPLAY

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

    return fpromise::ok(std::move(collection));
  }

  // Sets up the buffer collection information for collections that will be imported
  // into the engine.
  fuchsia::sysmem::BufferCollectionSyncPtr SetupClientTextures(
      DisplayCompositor* display_compositor, allocation::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::PixelFormatType pixel_type, uint32_t width, uint32_t height,
      uint32_t num_vmos, fuchsia::sysmem::BufferCollectionInfo_2* collection_info) {
    // Setup the buffer collection that will be used for the flatland rectangle's texture.
    auto texture_tokens = SysmemTokens::Create(sysmem_allocator_.get());

    auto result = display_compositor->ImportBufferCollection(
        collection_id, sysmem_allocator_.get(), std::move(texture_tokens.dup_token),
        BufferCollectionUsage::kClientImage, std::nullopt);
    EXPECT_TRUE(result);

    auto [buffer_usage, memory_constraints] = GetUsageAndMemoryConstraintsForCpuWriteOften();
    fuchsia::sysmem::BufferCollectionSyncPtr texture_collection =
        CreateBufferCollectionSyncPtrAndSetConstraints(
            sysmem_allocator_.get(), std::move(texture_tokens.local_token), num_vmos, width, height,
            buffer_usage, pixel_type, memory_constraints);

    // Have the client wait for buffers allocated so it can populate its information
    // struct with the vmo data.
    zx_status_t allocation_status = ZX_OK;
    auto status = texture_collection->WaitForBuffersAllocated(&allocation_status, collection_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);

    return texture_collection;
  }

  // Captures the pixel values on the display and reads them into |read_values|.
  void CaptureDisplayOutput(const fuchsia::sysmem::BufferCollectionInfo_2& collection_info,
                            uint64_t capture_image_id, std::vector<uint8_t>* read_values) {
    // Make sure the config from the DisplayCompositor has been completely applied first before
    // attempting to capture pixels from the display. This only matters for the real display.
    WaitOnVSync();

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
    EXPECT_TRUE(start_capture_result.is_response()) << start_capture_result.err();

    // We must wait for the capture to finish before we can proceed. Time out after 3 seconds.
    status = capture_signal_fence.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::msec(3000)),
                                           nullptr);
    EXPECT_EQ(status, ZX_OK);

    // Read the capture values back out.
    MapHostPointer(collection_info, /*vmo_index*/ 0,
                   [read_values](uint8_t* vmo_host, uint32_t num_bytes) mutable {
                     read_values->resize(num_bytes);
                     memcpy(read_values->data(), vmo_host, num_bytes);
                   });

    // Cleanup the capture.
    fuchsia::hardware::display::Controller_ReleaseCapture_Result result_capture_result;
    (*display_controller.get())->ReleaseCapture(capture_image_id, &result_capture_result);
    EXPECT_TRUE(result_capture_result.is_response());
  }

#ifdef FAKE_DISPLAY
  bool CaptureCompare(void* capture_buf, void* actual_buf, size_t size, uint32_t height,
                      uint32_t width) {
    EXPECT_EQ(size, width * height * 4);
    return memcmp(actual_buf, capture_buf, size) == 0;
  }
#else

  // This function is taken directly from the zircon display capture test and modified slightly
  // to fit this test.
  bool CaptureCompare(void* capture_buf, void* actual_buf, size_t size, uint32_t height,
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
    uint32_t buffer_stride = ZX_ALIGN(width * ZX_PIXEL_FORMAT_BYTES(kPixelFormat), 64);
    uint32_t capture_width_bytes = width * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_888);
    uint32_t buffer_width_bytes = width * ZX_PIXEL_FORMAT_BYTES(kPixelFormat);
    size_t buf_idx = 0;

#ifdef PLATFORM_ASTRO
    // For Astro only:
    // Ignore last column. Has junk (hardware bug)
    // Ignoring last column, means there is a shift by one pixel.
    // Therefore, image_buffer should start from pixel 1 (i.e. 4th byte since x888) and
    // capture_buffer should end at width - 3 (i.e. 888)
    capture_width_bytes -= ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_888);
    buf_idx = ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_x888);
#endif  // PLATFORM_ASTRO

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
#endif  // FAKE_DISPLAY
};

/** DIRECTIONS FOR WRITING TESTS
----------------------------------
When tests run on environments with a virtual gpu, please include this line in the top of the
test body:
    SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);

Furthermore, please make sure to use GTEST_SKIP() when appropriate to prevent display-controller
related failures that may happen when using fake display or on certain devices where some
display-controller functionality may not be implemented:

For example, when using display capture:

  if (capture_collection_result.is_error() &&
      capture_collection_result.error() == ZX_ERR_NOT_SUPPORTED) {
    GTEST_SKIP();
  }

And when importing textures to the display compositor:

  auto texture_collection =
      SetupClientTextures(display_compositor.get(), kTextureCollectionId, GetParam(), kTextureWidth,
                          kTextureHeight, 1, &texture_collection_info);
  if (!texture_collection) {
    GTEST_SKIP();
  }

If you are developing a test specifically for the DisplayController that does NOT need the
Vulkan Renderer, try creating a DisplayCompositor with the NullRenderer

  auto renderer = NewNullRenderer();
  auto display_compositor = std::make_shared<flatland::DisplayCompositor>(
      dispatcher(), display_manager_->default_display_controller(), renderer,
      utils::CreateSysmemAllocatorSyncPtr("display_compositor_pixeltest"),
      BufferCollectionImportMode::AttemptDisplayConstraints);

Lastly, if you are specifically testing the Vulkan Renderer and do not need Display Compositing, try
creating a DisplayCompositor BufferCollectionImportMode::RendererOnly:

   auto display_compositor = std::make_shared<flatland::DisplayCompositor>(
      dispatcher(), display_manager_->default_display_controller(), renderer,
      utils::CreateSysmemAllocatorSyncPtr("display_compositor_pixeltest"),
      BufferCollectionImportMode::RendererOnly);

When uploading a CL that makes changes to these tests, also make sure that they run on NUC
environments with basic envs. This should happen automatically because this is specified in
the build files but if it does not please add manually.
*/

class DisplayCompositorParameterizedPixelTest
    : public DisplayCompositorPixelTest,
      public ::testing::WithParamInterface<fuchsia::sysmem::PixelFormatType> {};

// Renders a fullscreen green rectangle to the provided display. This
// tests the engine's ability to properly read in flatland uberstruct
// data and then pass the data along to the display-controller interface
// to be composited directly in hardware. The Astro display controller
// only handles full screen rects.
VK_TEST_P(DisplayCompositorParameterizedPixelTest, FullscreenRectangleTest) {
  auto renderer = NewNullRenderer();
  auto display_compositor = std::make_shared<flatland::DisplayCompositor>(
      dispatcher(), display_manager_->default_display_controller(), renderer,
      utils::CreateSysmemAllocatorSyncPtr("display_compositor_pixeltest"),
      BufferCollectionImportMode::AttemptDisplayConstraints);

  auto display = display_manager_->default_display();
  auto display_controller = display_manager_->default_display_controller();

  const uint64_t kTextureCollectionId = allocation::GenerateUniqueBufferCollectionId();
  const uint64_t kCaptureCollectionId = allocation::GenerateUniqueBufferCollectionId();

  // Set up buffer collection and image for display_controller capture.
  uint64_t capture_image_id;
  fuchsia::sysmem::BufferCollectionInfo_2 capture_info;
  auto capture_collection_result =
      SetupCapture(kCaptureCollectionId, GetParam(), &capture_info, &capture_image_id);
  if (capture_collection_result.is_error() &&
      capture_collection_result.error() == ZX_ERR_NOT_SUPPORTED) {
    GTEST_SKIP();
  }
  EXPECT_TRUE(capture_collection_result.is_ok());
  auto capture_collection = std::move(capture_collection_result.value());

  // Setup the collection for the texture. Due to display controller limitations, the size of
  // the texture needs to match the size of the rect. So since we have a fullscreen rect, we
  // must also have a fullscreen texture to match.
  const uint32_t kRectWidth = display->width_in_px(), kTextureWidth = display->width_in_px();
  const uint32_t kRectHeight = display->height_in_px(), kTextureHeight = display->height_in_px();
  fuchsia::sysmem::BufferCollectionInfo_2 texture_collection_info;
  auto texture_collection =
      SetupClientTextures(display_compositor.get(), kTextureCollectionId, GetParam(), kTextureWidth,
                          kTextureHeight, 1, &texture_collection_info);
  if (!texture_collection) {
    GTEST_SKIP();
  }

  // Get a raw pointer for the texture's vmo and make it green.
  const uint32_t num_pixels = kTextureWidth * kTextureHeight;
  uint32_t col = (255U << 24) | (255U << 8);
  std::vector<uint32_t> write_values;
  write_values.assign(num_pixels, col);
  switch (GetParam()) {
    case fuchsia::sysmem::PixelFormatType::BGRA32:
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8: {
      MapHostPointer(texture_collection_info, /*vmo_index*/ 0,
                     [&write_values](uint8_t* vmo_host, uint32_t num_bytes) {
                       EXPECT_GE(num_bytes, sizeof(uint32_t) * write_values.size());
                       memcpy(vmo_host, write_values.data(),
                              sizeof(uint32_t) * write_values.size());
                     });
      break;
    }
    default:
      FX_NOTREACHED();
  }

  // Import the texture to the engine.
  auto image_metadata = ImageMetadata{.collection_id = kTextureCollectionId,
                                      .identifier = allocation::GenerateUniqueImageId(),
                                      .vmo_index = 0,
                                      .width = kTextureWidth,
                                      .height = kTextureHeight};
  auto result =
      display_compositor->ImportBufferImage(image_metadata, BufferCollectionUsage::kClientImage);
  EXPECT_TRUE(result);

  // We cannot send to display because it is not supported in allocations.
  if (!IsDisplaySupported(display_compositor.get(), kTextureCollectionId)) {
    GTEST_SKIP();
  }

  // Create a flatland session with a root and image handle. Import to the engine as display root.
  auto session = CreateSession();
  const TransformHandle root_handle = session.graph().CreateTransform();
  const TransformHandle image_handle = session.graph().CreateTransform();
  session.graph().AddChild(root_handle, image_handle);
  DisplayInfo display_info{
      .dimensions = glm::uvec2(display->width_in_px(), display->height_in_px()),
      .formats = {kPixelFormat}};
  display_compositor->AddDisplay(display, display_info, /*num_vmos*/ 0,
                                 /*out_buffer_collection*/ nullptr);

  // Setup the uberstruct data.
  auto uberstruct = session.CreateUberStructWithCurrentTopology(root_handle);
  uberstruct->images[image_handle] = image_metadata;
  uberstruct->local_matrices[image_handle] = glm::scale(
      glm::translate(glm::mat3(1.0), glm::vec2(0, 0)), glm::vec2(kRectWidth, kRectHeight));
  uberstruct->local_image_sample_regions[image_handle] = {0.f, 0.f, static_cast<float>(kRectWidth),
                                                          static_cast<float>(kRectHeight)};
  session.PushUberStruct(std::move(uberstruct));

  // Now we can finally render.
  display_compositor->RenderFrame(
      1, zx::time(1),
      GenerateDisplayListForTest(
          {{display->display_id(), std::make_pair(display_info, root_handle)}}),
      {}, [](const scheduling::FrameRenderer::Timestamps&) {});

  // Grab the capture vmo data.
  std::vector<uint8_t> read_values;
  CaptureDisplayOutput(capture_info, capture_image_id, &read_values);

  // Compare the capture vmo data to the texture data above. Since we're doing a full screen
  // render, the two should be identical. The comparison is a bit complicated though since
  // the images are of two different formats.
  bool images_are_same = CaptureCompare(read_values.data(), write_values.data(), read_values.size(),
                                        display->height_in_px(), display->width_in_px());
  EXPECT_TRUE(images_are_same);
}

// Renders a fullscreen green rectangle to the provided display using a solid color rect
// instead of an image. Use the NullRenderer to confirm this is being rendered through
// the display hardware.
VK_TEST_P(DisplayCompositorParameterizedPixelTest, FullscreenSolidColorRectangleTest) {
  auto renderer = NewNullRenderer();
  auto display_compositor = std::make_shared<flatland::DisplayCompositor>(
      dispatcher(), display_manager_->default_display_controller(), renderer,
      utils::CreateSysmemAllocatorSyncPtr("display_compositor_pixeltest"),
      BufferCollectionImportMode::AttemptDisplayConstraints);

  auto display = display_manager_->default_display();
  auto display_controller = display_manager_->default_display_controller();

  const uint64_t kCompareCollectionId = allocation::GenerateUniqueBufferCollectionId();
  const uint64_t kCaptureCollectionId = allocation::GenerateUniqueBufferCollectionId();

  // Set up buffer collection and image for display_controller capture.
  uint64_t capture_image_id;
  fuchsia::sysmem::BufferCollectionInfo_2 capture_info;
  auto capture_collection_result =
      SetupCapture(kCaptureCollectionId, GetParam(), &capture_info, &capture_image_id);
  if (capture_collection_result.is_error() &&
      capture_collection_result.error() == ZX_ERR_NOT_SUPPORTED) {
    GTEST_SKIP();
  }
  EXPECT_TRUE(capture_collection_result.is_ok());
  auto capture_collection = std::move(capture_collection_result.value());

  // Setup the collection for the texture. Due to display controller limitations, the size of
  // the texture needs to match the size of the rect. So since we have a fullscreen rect, we
  // must also have a fullscreen texture to match.
  const uint32_t kRectWidth = display->width_in_px(), kTextureWidth = display->width_in_px();
  const uint32_t kRectHeight = display->height_in_px(), kTextureHeight = display->height_in_px();
  fuchsia::sysmem::BufferCollectionInfo_2 compare_collection_info;
  auto compare_collection =
      SetupClientTextures(display_compositor.get(), kCompareCollectionId, GetParam(), kTextureWidth,
                          kTextureHeight, 1, &compare_collection_info);
  if (!compare_collection) {
    GTEST_SKIP();
  }

  // Get a raw pointer for the texture's vmo and make it green. Green is chosen because it has
  // the same bit offset in both RGBA and BGRA pixel formats. The display controller system is
  // also little-endian, so the BGRA values will be packed in an uint32_t as ARGB.
  const uint32_t num_pixels = kTextureWidth * kTextureHeight;
  uint32_t col = /*A*/ (255U << 24) | /*G*/ (51U << 8);
  std::vector<uint32_t> write_values;
  write_values.assign(num_pixels, col);
  switch (GetParam()) {
    case fuchsia::sysmem::PixelFormatType::BGRA32:
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8: {
      MapHostPointer(compare_collection_info, /*vmo_index*/ 0,
                     [&write_values](uint8_t* vmo_host, uint32_t num_bytes) {
                       EXPECT_GE(num_bytes, sizeof(uint32_t) * write_values.size());
                       memcpy(vmo_host, write_values.data(),
                              sizeof(uint32_t) * write_values.size());
                     });
      break;
    }
    default:
      FX_NOTREACHED();
  }

  // Import the texture to the engine. Set green to 0.2, which when converted to an
  // unnormalized uint8 value in the range [0,255] will be 51U.
  auto image_metadata = ImageMetadata{.identifier = allocation::kInvalidImageId,
                                      .multiply_color = {0, 0.2f, 0, 1},
                                      .blend_mode = fuchsia::ui::composition::BlendMode::SRC};

  // We cannot send to display because it is not supported in allocations.
  if (!IsDisplaySupported(display_compositor.get(), kCompareCollectionId)) {
    GTEST_SKIP();
  }

  // Create a flatland session with a root and image handle. Import to the engine as display root.
  auto session = CreateSession();
  const TransformHandle root_handle = session.graph().CreateTransform();
  const TransformHandle image_handle = session.graph().CreateTransform();
  session.graph().AddChild(root_handle, image_handle);
  DisplayInfo display_info{
      .dimensions = glm::uvec2(display->width_in_px(), display->height_in_px()),
      .formats = {kPixelFormat}};
  display_compositor->AddDisplay(display, display_info, /*num_vmos*/ 0,
                                 /*out_buffer_collection*/ nullptr);

  // Setup the uberstruct data.
  auto uberstruct = session.CreateUberStructWithCurrentTopology(root_handle);
  uberstruct->images[image_handle] = image_metadata;
  uberstruct->local_matrices[image_handle] = glm::scale(
      glm::translate(glm::mat3(1.0), glm::vec2(0, 0)), glm::vec2(kRectWidth, kRectHeight));
  uberstruct->local_image_sample_regions[image_handle] = {0.f, 0.f, static_cast<float>(kRectWidth),
                                                          static_cast<float>(kRectHeight)};
  session.PushUberStruct(std::move(uberstruct));

  // Now we can finally render.
  display_compositor->RenderFrame(
      1, zx::time(1),
      GenerateDisplayListForTest(
          {{display->display_id(), std::make_pair(display_info, root_handle)}}),
      {}, [](const scheduling::FrameRenderer::Timestamps&) {});

  // Grab the capture vmo data.
  std::vector<uint8_t> read_values;
  CaptureDisplayOutput(capture_info, capture_image_id, &read_values);

  // Compare the capture vmo data to the texture data above. Since we're doing a full screen
  // render, the two should be identical. The comparison is a bit complicated though since
  // the images are of two different formats.
  bool images_are_same = CaptureCompare(read_values.data(), write_values.data(), read_values.size(),
                                        display->height_in_px(), display->width_in_px());
  EXPECT_TRUE(images_are_same);
}

VK_TEST_P(DisplayCompositorParameterizedPixelTest, SetMinimumRGBTest) {
  auto renderer = NewNullRenderer();
  auto display_compositor = std::make_shared<flatland::DisplayCompositor>(
      dispatcher(), display_manager_->default_display_controller(), renderer,
      utils::CreateSysmemAllocatorSyncPtr("display_compositor_pixeltest"),
      BufferCollectionImportMode::AttemptDisplayConstraints);

  auto display = display_manager_->default_display();
  auto display_controller = display_manager_->default_display_controller();

  const uint64_t kCompareCollectionId = allocation::GenerateUniqueBufferCollectionId();
  const uint64_t kCaptureCollectionId = allocation::GenerateUniqueBufferCollectionId();

  // Set up buffer collection and image for display_controller capture.
  uint64_t capture_image_id;
  fuchsia::sysmem::BufferCollectionInfo_2 capture_info;
  auto capture_collection_result =
      SetupCapture(kCaptureCollectionId, GetParam(), &capture_info, &capture_image_id);
  if (capture_collection_result.is_error() &&
      capture_collection_result.error() == ZX_ERR_NOT_SUPPORTED) {
    GTEST_SKIP();
  }
  EXPECT_TRUE(capture_collection_result.is_ok());
  auto capture_collection = std::move(capture_collection_result.value());

  // Setup the collection for the texture. Due to display controller limitations, the size of
  // the texture needs to match the size of the rect. So since we have a fullscreen rect, we
  // must also have a fullscreen texture to match.
  const uint32_t kRectWidth = display->width_in_px(), kTextureWidth = display->width_in_px();
  const uint32_t kRectHeight = display->height_in_px(), kTextureHeight = display->height_in_px();
  fuchsia::sysmem::BufferCollectionInfo_2 compare_collection_info;
  auto compare_collection =
      SetupClientTextures(display_compositor.get(), kCompareCollectionId, GetParam(), kTextureWidth,
                          kTextureHeight, 1, &compare_collection_info);
  if (!compare_collection) {
    GTEST_SKIP();
  }

  const uint8_t kMinimum = 10U;

  // Get a raw pointer for the texture's vmo and make it the minimum color.
  const uint32_t num_pixels = kTextureWidth * kTextureHeight;
  std::vector<uint8_t> expected_values;
  expected_values.assign(num_pixels * 4, kMinimum);
  switch (GetParam()) {
    case fuchsia::sysmem::PixelFormatType::BGRA32:
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8: {
      MapHostPointer(compare_collection_info, /*vmo_index*/ 0,
                     [&expected_values](uint8_t* vmo_host, uint32_t num_bytes) {
                       EXPECT_GE(num_bytes, sizeof(uint8_t) * expected_values.size());
                       memcpy(vmo_host, expected_values.data(),
                              sizeof(uint8_t) * expected_values.size());
                     });
      break;
    }
    default:
      FX_NOTREACHED();
  }

  /// The metadata for the rectangle we shall be rendering below. There is no image -- so it is
  /// a solid-fill rectangle, with a pure black color (0,0,0,0). The goal here is to see if this
  /// black rectangle will be clamped to the minimum allowed value.
  auto image_metadata = ImageMetadata{.identifier = allocation::kInvalidImageId,
                                      .multiply_color = {0, 0, 0, 0},
                                      .blend_mode = fuchsia::ui::composition::BlendMode::SRC};

  // We cannot send to display because it is not supported in allocations.
  if (!IsDisplaySupported(display_compositor.get(), kCompareCollectionId)) {
    GTEST_SKIP();
  }

  // Create a flatland session with a root and image handle. Import to the engine as display root.
  auto session = CreateSession();
  const TransformHandle root_handle = session.graph().CreateTransform();
  const TransformHandle image_handle = session.graph().CreateTransform();
  session.graph().AddChild(root_handle, image_handle);
  DisplayInfo display_info{
      .dimensions = glm::uvec2(display->width_in_px(), display->height_in_px()),
      .formats = {kPixelFormat}};
  display_compositor->AddDisplay(display, display_info, /*num_vmos*/ 0,
                                 /*out_buffer_collection*/ nullptr);

  // Setup the uberstruct data.
  auto uberstruct = session.CreateUberStructWithCurrentTopology(root_handle);
  uberstruct->images[image_handle] = image_metadata;
  uberstruct->local_matrices[image_handle] = glm::scale(
      glm::translate(glm::mat3(1.0), glm::vec2(0, 0)), glm::vec2(kRectWidth, kRectHeight));
  uberstruct->local_image_sample_regions[image_handle] = {0.f, 0.f, static_cast<float>(kRectWidth),
                                                          static_cast<float>(kRectHeight)};
  session.PushUberStruct(std::move(uberstruct));

  display_compositor->SetMinimumRgb(kMinimum);

  // Now we can finally render.
  display_compositor->RenderFrame(
      1, zx::time(1),
      GenerateDisplayListForTest(
          {{display->display_id(), std::make_pair(display_info, root_handle)}}),
      {}, [](const scheduling::FrameRenderer::Timestamps&) {});

  // Grab the capture vmo data.
  std::vector<uint8_t> readback_values;
  CaptureDisplayOutput(capture_info, capture_image_id, &readback_values);

  // Compare the capture vmo data to the expected data above. Since we're doing a full screen
  // render, the two should be identical. The comparison is a bit complicated though since
  // the images are of two different formats.
  bool images_are_same =
      CaptureCompare(readback_values.data(), expected_values.data(), readback_values.size(),
                     display->height_in_px(), display->width_in_px());
  EXPECT_TRUE(images_are_same);
}

// TODO(fxbug.dev/74363): Add YUV formats when they are supported by fake or real display.
INSTANTIATE_TEST_SUITE_P(PixelFormats, DisplayCompositorParameterizedPixelTest,
                         ::testing::Values(fuchsia::sysmem::PixelFormatType::BGRA32,
                                           fuchsia::sysmem::PixelFormatType::R8G8B8A8));

class DisplayCompositorFallbackParameterizedPixelTest
    : public DisplayCompositorPixelTest,
      public ::testing::WithParamInterface<fuchsia::sysmem::PixelFormatType> {};

// Test the software path of the engine. Render 2 rectangles, each taking up half of the
// display's screen, so that the left half is blue and the right half is red.
VK_TEST_P(DisplayCompositorFallbackParameterizedPixelTest, SoftwareRenderingTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto display = display_manager_->default_display();
  auto display_controller = display_manager_->default_display_controller();

  const uint64_t kTextureCollectionId = allocation::GenerateUniqueBufferCollectionId();
  const uint64_t kCaptureCollectionId = allocation::GenerateUniqueBufferCollectionId();

  // Set up buffer collection and image for display_controller capture.
  uint64_t capture_image_id;
  fuchsia::sysmem::BufferCollectionInfo_2 capture_info;
  auto capture_collection_result =
      SetupCapture(kCaptureCollectionId, fuchsia::sysmem::PixelFormatType::BGRA32, &capture_info,
                   &capture_image_id);
  if (capture_collection_result.is_error() &&
      capture_collection_result.error() == ZX_ERR_NOT_SUPPORTED) {
    GTEST_SKIP();
  }
  EXPECT_TRUE(capture_collection_result.is_ok());
  auto capture_collection = std::move(capture_collection_result.value());

  // Setup the collection for the textures. Since we're rendering in software, we don't have to
  // deal with display limitations.
  const uint32_t kTextureWidth = 32, kTextureHeight = 32;
  fuchsia::sysmem::BufferCollectionInfo_2 texture_collection_info;

  // Create the image metadatas.
  ImageMetadata image_metadatas[2];
  for (uint32_t i = 0; i < 2; i++) {
    image_metadatas[i] = {.collection_id = kTextureCollectionId,
                          .identifier = allocation::GenerateUniqueImageId(),
                          .vmo_index = i,
                          .width = kTextureWidth,
                          .height = kTextureHeight,
                          .blend_mode = fuchsia::ui::composition::BlendMode::SRC};
  }

  // Use the VK renderer here so we can make use of software rendering.
  auto [escher, renderer] = NewVkRenderer();
  auto display_compositor = std::make_shared<flatland::DisplayCompositor>(
      dispatcher(), display_manager_->default_display_controller(), renderer,
      utils::CreateSysmemAllocatorSyncPtr("display_compositor_pixeltest"),
      BufferCollectionImportMode::AttemptDisplayConstraints);

  auto texture_collection = SetupClientTextures(display_compositor.get(), kTextureCollectionId,
                                                GetParam(), kTextureWidth, kTextureHeight,
                                                /*num_vmos*/ 2, &texture_collection_info);

  // Write to the two textures. Make the first blue and the second red.
  const uint32_t num_pixels = kTextureWidth * kTextureHeight;
  for (uint32_t i = 0; i < 2; i++) {
    MapHostPointer(
        texture_collection_info, /*vmo_index*/ i, [i](uint8_t* vmo_host, uint32_t num_bytes) {
          switch (GetParam()) {
            case fuchsia::sysmem::PixelFormatType::BGRA32: {
              const uint8_t kBlueBgraValues[] = {255U, 0U, 0U, 255U};
              const uint8_t kRedBgraValues[] = {0U, 0U, 255U, 255U};
              const uint8_t* cols = i == 0 ? kBlueBgraValues : kRedBgraValues;
              for (uint32_t p = 0; p < num_pixels * 4; ++p)
                vmo_host[p] = cols[p % 4];
              break;
            }
            case fuchsia::sysmem::PixelFormatType::R8G8B8A8: {
              const uint8_t kBlueRgbaValues[] = {0U, 0U, 255U, 255U};
              const uint8_t kRedRgbaValues[] = {255U, 0U, 0U, 255U};
              const uint8_t* cols = i == 0 ? kBlueRgbaValues : kRedRgbaValues;
              for (uint32_t p = 0; p < num_pixels * 4; ++p)
                vmo_host[p] = cols[p % 4];
              break;
            }
            case fuchsia::sysmem::PixelFormatType::NV12: {
              const uint8_t kBlueYuvValues[] = {29U, 255U, 107U};
              const uint8_t kRedYuvValues[] = {76U, 84U, 255U};
              const uint8_t* cols = i == 0 ? kBlueYuvValues : kRedYuvValues;
              for (uint32_t p = 0; p < num_pixels; ++p)
                vmo_host[p] = cols[0];
              for (uint32_t p = num_pixels; p < num_pixels + num_pixels / 2; p += 2) {
                vmo_host[p] = cols[1];
                vmo_host[p + 1] = cols[2];
              }
              break;
            }
            case fuchsia::sysmem::PixelFormatType::I420: {
              const uint8_t kBlueYuvValues[] = {29U, 255U, 107U};
              const uint8_t kRedYuvValues[] = {76U, 84U, 255U};
              const uint8_t* cols = i == 0 ? kBlueYuvValues : kRedYuvValues;
              for (uint32_t p = 0; p < num_pixels; ++p)
                vmo_host[p] = cols[0];
              for (uint32_t p = num_pixels; p < num_pixels + num_pixels / 4; ++p)
                vmo_host[p] = cols[1];
              for (uint32_t p = num_pixels + num_pixels / 4; p < num_pixels + num_pixels / 2; ++p)
                vmo_host[p] = cols[2];
              break;
            }
            default:
              FX_NOTREACHED();
          }
        });
  }

  // We now have to import the textures to the engine and the renderer.
  for (uint32_t i = 0; i < 2; i++) {
    auto result = display_compositor->ImportBufferImage(image_metadatas[i],
                                                        BufferCollectionUsage::kClientImage);
    EXPECT_TRUE(result);
  }

  fuchsia::sysmem::BufferCollectionInfo_2 render_target_info;
  DisplayInfo display_info{
      .dimensions = glm::uvec2(display->width_in_px(), display->height_in_px()),
      .formats = {kPixelFormat}};
  display_compositor->AddDisplay(display, display_info, /*num_vmos*/ 2, &render_target_info);

  // Now we can finally render.
  RenderData render_data;
  {
    uint32_t width = display->width_in_px() / 2;
    uint32_t height = display->height_in_px();

    render_data.display_id = display->display_id();
    render_data.rectangles.emplace_back(glm::vec2(0), glm::vec2(width, height));
    render_data.rectangles.emplace_back(glm::vec2(width, 0), glm::vec2(width, height));

    render_data.images.push_back(image_metadatas[0]);
    render_data.images.push_back(image_metadatas[1]);
  }
  display_compositor->RenderFrame(1, zx::time(1), {std::move(render_data)}, {},
                                  [](const scheduling::FrameRenderer::Timestamps&) {});
  renderer->WaitIdle();

  // Make sure the render target has the same data as what's being put on the display.
  MapHostPointer(render_target_info, /*vmo_index*/ 0, [&](uint8_t* vmo_host, uint32_t num_bytes) {
    // Grab the capture vmo data.
    std::vector<uint8_t> read_values;
    CaptureDisplayOutput(capture_info, capture_image_id, &read_values);

    // Compare the capture vmo data to the values we are expecting.
    bool images_are_same = CaptureCompare(read_values.data(), vmo_host, read_values.size(),
                                          display->height_in_px(), display->width_in_px());
    EXPECT_TRUE(images_are_same);

    // Make sure that the vmo_host has the right amount of blue and red colors, so
    // that we know that even if the display matches the render target, that its not
    // just because both are black or some other wrong colors.
    uint32_t num_blue = 0, num_red = 0;
    uint32_t num_pixels = num_bytes / 4;
    for (uint32_t i = 0; i < num_pixels; i++) {
      // |vmo_host| has BGRA sequence in pixel values.
      if (vmo_host[4 * i] == 255U) {
        num_blue++;
      } else if (vmo_host[4 * i + 2] == 255U) {
        num_red++;
      }
    }

    // Due to image formating, the number of "pixels" in the image above might not be the same as
    // the number of pixels that are actually on the screen. So here we make sure that exactly
    // half the screen is blue, and the other half is red.
    uint32_t num_screen_pixels = display->width_in_px() * display->height_in_px();
    EXPECT_EQ(num_blue, num_screen_pixels / 2);
    EXPECT_EQ(num_red, num_screen_pixels / 2);
  });
}

INSTANTIATE_TEST_SUITE_P(PixelFormats, DisplayCompositorFallbackParameterizedPixelTest,
                         ::testing::Values(fuchsia::sysmem::PixelFormatType::BGRA32,
                                           fuchsia::sysmem::PixelFormatType::R8G8B8A8,
                                           fuchsia::sysmem::PixelFormatType::NV12,
                                           fuchsia::sysmem::PixelFormatType::I420));

// Test to make sure that the engine can handle rendering a transparent object overlapping an
// opaque one.
VK_TEST_F(DisplayCompositorPixelTest, OverlappingTransparencyTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto display = display_manager_->default_display();
  auto display_controller = display_manager_->default_display_controller();

  const uint64_t kTextureCollectionId = allocation::GenerateUniqueBufferCollectionId();
  const uint64_t kCaptureCollectionId = allocation::GenerateUniqueBufferCollectionId();

  // Set up buffer collection and image for display_controller capture.
  uint64_t capture_image_id;
  fuchsia::sysmem::BufferCollectionInfo_2 capture_info;
  auto capture_collection_result =
      SetupCapture(kCaptureCollectionId, fuchsia::sysmem::PixelFormatType::BGRA32, &capture_info,
                   &capture_image_id);
  if (capture_collection_result.is_error() &&
      capture_collection_result.error() == ZX_ERR_NOT_SUPPORTED) {
    GTEST_SKIP();
  }
  EXPECT_TRUE(capture_collection_result.is_ok());
  auto capture_collection = std::move(capture_collection_result.value());

  // Setup the collection for the textures. Since we're rendering in software, we don't have to
  // deal with display limitations.
  const uint32_t kTextureWidth = 1, kTextureHeight = 1;
  fuchsia::sysmem::BufferCollectionInfo_2 texture_collection_info;

  // Create the image metadatas.
  ImageMetadata image_metadatas[2];
  for (uint32_t i = 0; i < 2; i++) {
    auto blend_mode = (i != 1) ? fuchsia::ui::composition::BlendMode::SRC
                               : fuchsia::ui::composition::BlendMode::SRC_OVER;
    image_metadatas[i] = {.collection_id = kTextureCollectionId,
                          .identifier = allocation::GenerateUniqueImageId(),
                          .vmo_index = i,
                          .width = kTextureWidth,
                          .height = kTextureHeight,
                          .blend_mode = blend_mode};
  }

  // Use the VK renderer here so we can make use of software rendering.
  auto [escher, renderer] = NewVkRenderer();
  auto display_compositor = std::make_shared<flatland::DisplayCompositor>(
      dispatcher(), display_manager_->default_display_controller(), renderer,
      utils::CreateSysmemAllocatorSyncPtr("display_compositor_pixeltest"),
      BufferCollectionImportMode::AttemptDisplayConstraints);

  auto texture_collection =
      SetupClientTextures(display_compositor.get(), kTextureCollectionId,
                          fuchsia::sysmem::PixelFormatType::BGRA32, kTextureWidth, kTextureHeight,
                          /*num_vmos*/ 2, &texture_collection_info);

  // Write to the two textures. Make the first blue and opaque and the second red and
  // half transparent. Format is ARGB.
  uint32_t cols[] = {(255 << 24) | (255U << 0), (128 << 24) | (255U << 16)};
  for (uint32_t i = 0; i < 2; i++) {
    std::vector<uint32_t> write_values;
    write_values.assign(kTextureWidth * kTextureHeight, cols[i]);
    MapHostPointer(texture_collection_info, /*vmo_index*/ i,
                   [write_values](uint8_t* vmo_host, uint32_t num_bytes) {
                     EXPECT_TRUE(num_bytes >= sizeof(uint32_t) * write_values.size());
                     memcpy(vmo_host, write_values.data(), sizeof(uint32_t) * write_values.size());
                   });
  }

  // We now have to import the textures to the engine and the renderer.
  for (uint32_t i = 0; i < 2; i++) {
    auto result = display_compositor->ImportBufferImage(image_metadatas[i],
                                                        BufferCollectionUsage::kClientImage);
    EXPECT_TRUE(result);
  }

  fuchsia::sysmem::BufferCollectionInfo_2 render_target_info;
  DisplayInfo display_info{
      .dimensions = glm::uvec2(display->width_in_px(), display->height_in_px()),
      .formats = {kPixelFormat}};
  display_compositor->AddDisplay(display, display_info, /*num_vmos*/ 2, &render_target_info);

  // Now we can finally render.
  const uint32_t kNumOverlappingRows = 25;
  RenderData render_data;
  {
    uint32_t width = display->width_in_px() / 2;
    uint32_t height = display->height_in_px();

    // Have the two rectangles overlap each other slightly with 25 rows in common across the
    // displays.
    render_data.display_id = display->display_id();
    render_data.rectangles.push_back(
        {glm::vec2(0, 0), glm::vec2(width + kNumOverlappingRows, height)});
    render_data.rectangles.push_back({glm::vec2(width - kNumOverlappingRows, 0),
                                      glm::vec2(width + kNumOverlappingRows, height)});

    render_data.images.push_back(image_metadatas[0]);
    render_data.images.push_back(image_metadatas[1]);
  }
  display_compositor->RenderFrame(1, zx::time(1), {std::move(render_data)}, {},
                                  [](const scheduling::FrameRenderer::Timestamps&) {});
  renderer->WaitIdle();

  // Make sure the render target has the same data as what's being put on the display.
  MapHostPointer(render_target_info, /*vmo_index*/ 0, [&](uint8_t* vmo_host, uint32_t num_bytes) {
    // Grab the capture vmo data.
    std::vector<uint8_t> read_values;
    CaptureDisplayOutput(capture_info, capture_image_id, &read_values);

    // Compare the capture vmo data to the values we are expecting.
    bool images_are_same = CaptureCompare(read_values.data(), vmo_host, read_values.size(),
                                          display->height_in_px(), display->width_in_px());
    EXPECT_TRUE(images_are_same);

    // Make sure that the vmo_host has the right amount of blue and red colors, so
    // that we know that even if the display matches the render target, that its not
    // just because both are black or some other wrong colors.
    uint32_t num_blue = 0, num_red = 0, num_overlap = 0;
    uint32_t num_pixels = num_bytes / 4;
    uint32_t* host_ptr = reinterpret_cast<uint32_t*>(vmo_host);
    for (uint32_t i = 0; i < num_pixels; i++) {
      uint32_t curr_col = host_ptr[i];
      if (curr_col == cols[0]) {
        num_blue++;
      } else if (curr_col == cols[1]) {
        num_red++;
      } else if (curr_col != 0) {
        num_overlap++;
      }
    }

    // Due to image formating, the number of "pixels" in the image above might not be the same as
    // the number of pixels that are actually on the screen.
    uint32_t num_screen_pixels =
        (display->width_in_px() / 2 - kNumOverlappingRows) * display->height_in_px();
    EXPECT_EQ(num_blue, num_screen_pixels);
    EXPECT_EQ(num_red, num_screen_pixels);
    EXPECT_EQ(num_overlap,
              (display->width_in_px() * display->height_in_px()) - 2 * num_screen_pixels);
  });
}

class DisplayCompositorParameterizedTest
    : public DisplayCompositorPixelTest,
      public ::testing::WithParamInterface<fuchsia::sysmem::PixelFormatType> {};

// TODO(fxbug.dev/74363): Add YUV formats when they are supported by fake or real display.
INSTANTIATE_TEST_SUITE_P(PixelFormats, DisplayCompositorParameterizedTest,
                         ::testing::Values(fuchsia::sysmem::PixelFormatType::BGRA32));

// Pixel test for making sure that multiparented transforms render properly.
// This is for A11Y Magnification.
//
// For this test we are going to render the same colored square twice: once on the left side of
// the screen at regular resolution and once on the right at a magnified resolution. The original
// will be (2,2) and the magnified one will have a scale factor of 2 applied, so it will become
// (4,4). However both squares will in actuality be the same transform/image in the flatland scene
// graph and uber struct. It is simply that the transform has two parents, which causes it to be
// duplicated in the topology vector. The top-left corner of the square has been marked a different
// color from the rest of the square in order to guarantee the orientation of the magnified render.
//
// - - - - - - - - - -
// - B W - - B B W W -
// - W W - - B B W W -
// - - - - - W W W W -
// - - - - - W W W W -
// - - - - - - - - - -
//
VK_TEST_P(DisplayCompositorParameterizedTest, MultipleParentPixelTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  auto display = display_manager_->default_display();
  auto display_controller = display_manager_->default_display_controller();

  // Use the VK renderer here so we can make use of software rendering.
  auto [escher, renderer] = NewVkRenderer();
  auto display_compositor = std::make_shared<flatland::DisplayCompositor>(
      dispatcher(), display_manager_->default_display_controller(), renderer,
      utils::CreateSysmemAllocatorSyncPtr("display_compositor_pixeltest"),
      BufferCollectionImportMode::RendererOnly);

  const uint64_t kTextureCollectionId = allocation::GenerateUniqueBufferCollectionId();
  const uint64_t kCaptureCollectionId = allocation::GenerateUniqueBufferCollectionId();

  // Set up buffer collection and image for display_controller capture.
  uint64_t capture_image_id;
  fuchsia::sysmem::BufferCollectionInfo_2 capture_info;
  auto capture_collection_result =
      SetupCapture(kCaptureCollectionId, GetParam(), &capture_info, &capture_image_id);
  if (capture_collection_result.is_error() &&
      capture_collection_result.error() == ZX_ERR_NOT_SUPPORTED) {
    GTEST_SKIP();
  }

  EXPECT_TRUE(capture_collection_result.is_ok());
  auto capture_collection = std::move(capture_collection_result.value());

  // Setup the collection for the textures. Since we're rendering in software, we don't have to
  // deal with display limitations.
  const uint32_t kTextureWidth = 2, kTextureHeight = 2;
  fuchsia::sysmem::BufferCollectionInfo_2 texture_collection_info;

  // Create the texture's metadata.
  ImageMetadata image_metadata = {.collection_id = kTextureCollectionId,
                                  .identifier = allocation::GenerateUniqueImageId(),
                                  .vmo_index = 0,
                                  .width = kTextureWidth,
                                  .height = kTextureHeight,
                                  .blend_mode = fuchsia::ui::composition::BlendMode::SRC};

  auto texture_collection =
      SetupClientTextures(display_compositor.get(), kTextureCollectionId, GetParam(), 60, 40,
                          /*num_vmos*/ 1, &texture_collection_info);

  switch (GetParam()) {
    case fuchsia::sysmem::PixelFormatType::BGRA32: {
      MapHostPointer(
          texture_collection_info, /*vmo_index*/ 0, [](uint8_t* vmo_host, uint32_t num_bytes) {
            const uint8_t kBlueBgraValues[] = {255U, 0U, 0U, 255U};
            const uint8_t kWhiteBgraValues[] = {255U, 255U, 255U, 255U};

            for (uint32_t p = 0; p < num_bytes; ++p) {
              // Make the first pixel blue, and the rest white.
              const uint8_t* cols = (p < 4) ? kBlueBgraValues : kWhiteBgraValues;
              vmo_host[p] = cols[p % 4];
            }

            // Flush the cache after writing to host VMO.
            EXPECT_EQ(ZX_OK, zx_cache_flush(vmo_host, num_bytes,
                                            ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));
          });

      break;
    }
    default:
      FX_NOTREACHED();
  }

  auto result =
      display_compositor->ImportBufferImage(image_metadata, BufferCollectionUsage::kClientImage);
  EXPECT_TRUE(result);

  // We cannot send to display because it is not supported in allocations.
  if (!IsDisplaySupported(display_compositor.get(), kTextureCollectionId) || !texture_collection) {
    GTEST_SKIP();
  }

  // Create a flatland session to represent a graph that has magnification applied.
  auto session = CreateSession();
  const TransformHandle root_handle = session.graph().CreateTransform();
  const TransformHandle parent_1_handle = session.graph().CreateTransform();
  const TransformHandle parent_2_handle = session.graph().CreateTransform();
  const TransformHandle child_handle = session.graph().CreateTransform();

  session.graph().AddChild(root_handle, parent_1_handle);
  session.graph().AddChild(root_handle, parent_2_handle);
  session.graph().AddChild(parent_1_handle, child_handle);
  session.graph().AddChild(parent_2_handle, child_handle);

  fuchsia::sysmem::BufferCollectionInfo_2 render_target_info;
  DisplayInfo display_info{
      .dimensions = glm::uvec2(display->width_in_px(), display->height_in_px()),
      .formats = {kPixelFormat}};
  display_compositor->AddDisplay(display, display_info, /*num_vmos*/ 2, &render_target_info);

  // Setup the uberstruct data.
  auto uberstruct = session.CreateUberStructWithCurrentTopology(root_handle);
  {
    uberstruct->images[child_handle] = image_metadata;

    // The first parent will have (1,1) scale and no translation.
    uberstruct->local_matrices[parent_1_handle] =
        glm::scale(glm::translate(glm::mat3(1.0), glm::vec2(0, 0)), glm::vec2(1, 1));

    // The second parent will have a(2, 2) scale and a translation applied to it to
    // shift it to the right.
    uberstruct->local_matrices[parent_2_handle] =
        glm::scale(glm::translate(glm::mat3(1.0), glm::vec2(10, 0)), glm::vec2(2, 2));

    // The child has a built in scale of 2x2.
    uberstruct->local_matrices[child_handle] = glm::scale(glm::mat3(1.0), glm::vec2(2, 2));
    uberstruct->local_image_sample_regions[child_handle] = {
        0.f, 0.f, static_cast<float>(kTextureWidth), static_cast<float>(kTextureHeight)};
    session.PushUberStruct(std::move(uberstruct));
  }

  // Now we can finally render.
  display_compositor->RenderFrame(
      1, zx::time(1),
      GenerateDisplayListForTest(
          {{display->display_id(), std::make_pair(display_info, root_handle)}}),
      {}, [](const scheduling::FrameRenderer::Timestamps&) {});
  renderer->WaitIdle();

  // Make sure the render target has the same data as what's being put on the display.
  MapHostPointer(render_target_info, /*vmo_index*/ 0, [&](uint8_t* vmo_host, uint32_t num_bytes) {
    // Grab the capture vmo data.
    std::vector<uint8_t> read_values;
    CaptureDisplayOutput(capture_info, capture_image_id, &read_values);

    // Compare the capture vmo data to the values we are expecting.
    bool images_are_same = CaptureCompare(read_values.data(), vmo_host, read_values.size(),
                                          display->height_in_px(), display->width_in_px());
    EXPECT_TRUE(images_are_same);

    auto get_pixel = [&display](uint8_t* vmo_host, uint32_t x, uint32_t y) -> uint32_t {
      uint32_t index = y * display->width_in_px() * 4 + x * 4;
      auto a = vmo_host[index];
      auto b = vmo_host[index + 1];
      auto c = vmo_host[index + 2];
      auto d = vmo_host[index + 3];
      return (a << 24) | (b << 16) | (c << 8) | d;
    };

    // There should be a total of 20 white pixels (4 for the normal white square and
    // 16 for the magnified white square).
    uint32_t num_white = 0, num_blue = 0;
    uint32_t num_pixels = num_bytes / 4;
    const uint32_t kWhiteColor = 0xFFFFFFFF;
    const uint32_t kBlueColor = 0xFF0000FF;
    for (uint32_t i = 0; i < num_pixels; i += 4) {
      // |vmo_host| has BGRA sequence in pixel values.
      auto a = vmo_host[i];
      auto b = vmo_host[i + 1];
      auto c = vmo_host[i + 2];
      auto d = vmo_host[i + 3];
      uint32_t val = (a << 24) | (b << 16) | (c << 8) | d;
      if (val == kWhiteColor) {
        num_white++;
      } else if (val == kBlueColor) {
        num_blue++;
      }
    }
    EXPECT_EQ(num_white, 15U);
    EXPECT_EQ(num_blue, 5U);

    // Expect the top-left corner of the mag rect to be blue.
    EXPECT_EQ(get_pixel(vmo_host, 10, 0), kBlueColor);
  });
}

}  // namespace test
}  // namespace flatland
