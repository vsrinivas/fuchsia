// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/framebuffer/framebuffer.h"

#include <fcntl.h>
#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/image-format-llcpp/image-format-llcpp.h>
#include <zircon/pixelformat.h>

#include <thread>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace fhd = llcpp::fuchsia::hardware::display;
namespace sysmem = ::llcpp::fuchsia::sysmem;

zx_status_t fb_bind_with_channel(bool single_buffer, const char** err_msg_out,
                                 zx::channel dc_client_channel);

void RunSingleBufferTest() {
  fbl::unique_fd dc_fd(open("/dev/class/display-controller/000", O_RDWR));
  if (!dc_fd) {
    fprintf(stdout, "Skipping test because of no display controller\n");
    return;
  }
  dc_fd.reset();
  constexpr uint32_t kIterations = 2;

  for (uint32_t i = 0; i < kIterations; i++) {
    const char* error;
    zx_status_t status = fb_bind(true, &error);
    if (status == ZX_ERR_NOT_SUPPORTED) {
      // If the simple display driver is being used then sysmem isn't supported
      // and libframebuffer isn't either.
      fprintf(stderr, "Skipping because received ZX_ERR_NOT_SUPPORTED\n");
      return;
    }
    ASSERT_OK(status);
    zx_handle_t buffer_handle = fb_get_single_buffer();
    EXPECT_NE(ZX_HANDLE_INVALID, buffer_handle);

    uint32_t width, height, linear_stride_px;
    zx_pixel_format_t format;
    fb_get_config(&width, &height, &linear_stride_px, &format);
    EXPECT_LE(width, linear_stride_px);
    EXPECT_LT(0u, ZX_PIXEL_FORMAT_BYTES(format));

    uint64_t buffer_size;
    EXPECT_OK(zx_vmo_get_size(buffer_handle, &buffer_size));
    EXPECT_LE(linear_stride_px * ZX_PIXEL_FORMAT_BYTES(format) * height, buffer_size);

    fb_release();
  }
}

TEST(Framebuffer, SingleBuffer) {
  zx::event finished;
  zx::event::create(0, &finished);
  std::thread execute_thread([&finished]() {
    RunSingleBufferTest();
    finished.signal(0, ZX_USER_SIGNAL_0);
  });
  zx_status_t status =
      finished.wait_one(ZX_USER_SIGNAL_0, zx::deadline_after(zx::sec(60)), nullptr);
  EXPECT_EQ(ZX_OK, status);
  if (status != ZX_OK) {
    fprintf(stderr, "Test timed out. Maybe no display is connected to device.\n");
    execute_thread.detach();
  } else {
    execute_thread.join();
  }
}

namespace {

constexpr uint32_t kBytesPerRowDivisor = 128;

class StubDisplayController : public fhd::Controller::Interface {
 public:
  StubDisplayController(bool use_ram_domain) : use_ram_domain_(use_ram_domain) {
    zx::channel sysmem_server, sysmem_client;
    ASSERT_OK(zx::channel::create(0, &sysmem_server, &sysmem_client));
    ASSERT_OK(fdio_service_connect("/svc/fuchsia.sysmem.Allocator", sysmem_server.release()));

    sysmem_allocator_ = std::make_unique<sysmem::Allocator::SyncClient>(std::move(sysmem_client));
  }

  ~StubDisplayController() { current_buffer_collection_->Close(); }
  void ImportVmoImage(fhd::ImageConfig image_config, ::zx::vmo vmo, int32_t offset,
                      ImportVmoImageCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void ImportImage(fhd::ImageConfig image_config, uint64_t collection_id, uint32_t index,
                   ImportImageCompleter::Sync _completer) override {
    _completer.Reply(ZX_OK, 1);
  }
  void ReleaseImage(uint64_t image_id, ReleaseImageCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void ImportEvent(::zx::event event, uint64_t id, ImportEventCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void ReleaseEvent(uint64_t id, ReleaseEventCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void CreateLayer(CreateLayerCompleter::Sync _completer) override { _completer.Reply(ZX_OK, 1); }

  void DestroyLayer(uint64_t layer_id, DestroyLayerCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void ImportGammaTable(uint64_t gamma_table_id, ::fidl::Array<float, 256> r,
                        ::fidl::Array<float, 256> g, ::fidl::Array<float, 256> b,
                        ImportGammaTableCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void ReleaseGammaTable(uint64_t gamma_table_id,
                         ReleaseGammaTableCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void SetDisplayMode(uint64_t display_id, fhd::Mode mode,
                      SetDisplayModeCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void SetDisplayColorConversion(uint64_t display_id, ::fidl::Array<float, 3> preoffsets,
                                 ::fidl::Array<float, 9> coefficients,
                                 ::fidl::Array<float, 3> postoffsets,
                                 SetDisplayColorConversionCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void SetDisplayGammaTable(uint64_t display_id, uint64_t gamma_table_id,
                            SetDisplayGammaTableCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void SetDisplayLayers(uint64_t display_id, ::fidl::VectorView<uint64_t> layer_ids,
                        SetDisplayLayersCompleter::Sync _completer) override {
    // Ignore
  }

  void SetLayerPrimaryConfig(uint64_t layer_id, fhd::ImageConfig image_config,
                             SetLayerPrimaryConfigCompleter::Sync _completer) override {
    // Ignore
  }

  void SetLayerPrimaryPosition(uint64_t layer_id, fhd::Transform transform, fhd::Frame src_frame,
                               fhd::Frame dest_frame,
                               SetLayerPrimaryPositionCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerPrimaryAlpha(uint64_t layer_id, fhd::AlphaMode mode, float val,
                            SetLayerPrimaryAlphaCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerCursorConfig(uint64_t layer_id, fhd::ImageConfig image_config,
                            SetLayerCursorConfigCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerCursorPosition(uint64_t layer_id, int32_t x, int32_t y,
                              SetLayerCursorPositionCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerColorConfig(uint64_t layer_id, uint32_t pixel_format,
                           ::fidl::VectorView<uint8_t> color_bytes,
                           SetLayerColorConfigCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerImage(uint64_t layer_id, uint64_t image_id, uint64_t wait_event_id,
                     uint64_t signal_event_id, SetLayerImageCompleter::Sync _completer) override {
    // Ignore
  }

  void CheckConfig(bool discard, CheckConfigCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void ApplyConfig(ApplyConfigCompleter::Sync _completer) override {
    // Ignore
  }

  void EnableVsync(bool enable, EnableVsyncCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void SetVirtconMode(uint8_t mode, SetVirtconModeCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void ImportBufferCollection(uint64_t collection_id, ::zx::channel collection_token,
                              ImportBufferCollectionCompleter::Sync _completer) override {
    zx::channel server, client;
    ASSERT_OK(zx::channel::create(0, &server, &client));

    ASSERT_TRUE(
        sysmem_allocator_->BindSharedCollection(std::move(collection_token), std::move(server))
            .ok());
    current_buffer_collection_ =
        std::make_unique<sysmem::BufferCollection::SyncClient>(std::move(client));

    _completer.Reply(ZX_OK);
  }
  void ReleaseBufferCollection(uint64_t collection_id,
                               ReleaseBufferCollectionCompleter::Sync _completer) override {}

  void SetBufferCollectionConstraints(
      uint64_t collection_id, fhd::ImageConfig config,
      SetBufferCollectionConstraintsCompleter::Sync _completer) override {
    sysmem::BufferCollectionConstraints constraints;
    constraints.usage.cpu = sysmem::cpuUsageWriteOften | sysmem::cpuUsageRead;
    constraints.min_buffer_count = 1;
    constraints.image_format_constraints_count = 1;
    auto& image_constraints = constraints.image_format_constraints[0];
    image_constraints = image_format::GetDefaultImageFormatConstraints();
    image_constraints.pixel_format.type = sysmem::PixelFormatType::BGRA32;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value = sysmem::FORMAT_MODIFIER_LINEAR;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = sysmem::ColorSpaceType::SRGB;
    image_constraints.max_coded_width = 0xffffffff;
    image_constraints.max_coded_height = 0xffffffff;
    image_constraints.min_bytes_per_row = 0;
    image_constraints.max_bytes_per_row = 0xffffffff;
    image_constraints.bytes_per_row_divisor = kBytesPerRowDivisor;

    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints = image_format::GetDefaultBufferMemoryConstraints();
    constraints.buffer_memory_constraints.ram_domain_supported = use_ram_domain_;
    constraints.buffer_memory_constraints.cpu_domain_supported = !use_ram_domain_;

    current_buffer_collection_->SetConstraints(true, constraints);
    _completer.Reply(ZX_OK);
  }

  void GetSingleBufferFramebuffer(GetSingleBufferFramebufferCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void IsCaptureSupported(IsCaptureSupportedCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void ImportImageForCapture(fhd::ImageConfig image_config, uint64_t collection_id, uint32_t index,
                             ImportImageForCaptureCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void StartCapture(uint64_t signal_event_id, uint64_t image_id,
                    StartCaptureCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void ReleaseCapture(uint64_t image_id, ReleaseCaptureCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void AcknowledgeVsync(uint64_t cookie, AcknowledgeVsyncCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void SetMinimumRgb(uint8_t minimum_rgb, SetMinimumRgbCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

 private:
  std::unique_ptr<sysmem::Allocator::SyncClient> sysmem_allocator_;
  std::unique_ptr<sysmem::BufferCollection::SyncClient> current_buffer_collection_;
  bool use_ram_domain_;
};

}  // namespace

void SendInitialDisplay(const zx::channel& server_channel, fhd::Mode* mode, uint32_t pixel_format) {
  fhd::Info info;
  info.pixel_format = fidl::VectorView(fidl::unowned_ptr(&pixel_format), 1);
  info.modes = fidl::VectorView(fidl::unowned_ptr(mode), 1);
  fidl::VectorView<fhd::Info> added(fidl::unowned_ptr(&info), 1);
  fidl::VectorView<uint64_t> removed;

  ASSERT_OK(fhd::Controller::SendOnDisplaysChangedEvent(zx::unowned_channel(server_channel),
                                                        std::move(added), std::move(removed)));
}

void TestDisplayStride(bool ram_domain) {
  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  StubDisplayController controller(ram_domain);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fhd::Mode mode;
  mode.horizontal_resolution = 301;
  mode.vertical_resolution = 250;
  constexpr uint32_t kPixelFormat = ZX_PIXEL_FORMAT_ARGB_8888;
  SendInitialDisplay(server_channel, &mode, kPixelFormat);

  loop.StartThread();

  ASSERT_OK(
      fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(server_channel), &controller));

  const char* error;
  zx_status_t status = fb_bind_with_channel(true, &error, std::move(client_channel));
  EXPECT_OK(status);
  zx_handle_t buffer_handle = fb_get_single_buffer();
  EXPECT_NE(ZX_HANDLE_INVALID, buffer_handle);

  uint32_t width, height, linear_stride_px;
  zx_pixel_format_t format;
  fb_get_config(&width, &height, &linear_stride_px, &format);
  EXPECT_EQ(mode.horizontal_resolution, width);
  EXPECT_EQ(mode.vertical_resolution, height);
  EXPECT_EQ(kPixelFormat, format);

  constexpr uint32_t kBytesPerPixel = 4;

  // Round up to be a multiple of kBytesPerRowDivisor bytes.
  EXPECT_EQ(fbl::round_up(width * kBytesPerPixel, kBytesPerRowDivisor) / kBytesPerPixel,
            linear_stride_px);

  uint64_t buffer_size;
  EXPECT_OK(zx_vmo_get_size(buffer_handle, &buffer_size));
  EXPECT_LE(linear_stride_px * ZX_PIXEL_FORMAT_BYTES(format) * height, buffer_size);
}

// Check that the correct stride is returned when a weird one is used.
TEST(Framebuffer, DisplayStrideCpuDomain) { TestDisplayStride(false); }

TEST(Framebuffer, DisplayStrideRamDomain) { TestDisplayStride(true); }
