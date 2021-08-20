// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/framebuffer/framebuffer.h"

#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/image-format-llcpp/image-format-llcpp.h>
#include <lib/service/llcpp/service.h>

#include <thread>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "src/lib/fsl/handles/object_info.h"

namespace fhd = fuchsia_hardware_display;
namespace sysmem = fuchsia_sysmem;

zx_status_t fb_bind_with(bool single_buffer, const char** err_msg_out,
                         fidl::ClientEnd<fhd::Controller> client);

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

class StubDisplayController : public fidl::WireServer<fhd::Controller> {
 public:
  StubDisplayController(bool use_ram_domain) : use_ram_domain_(use_ram_domain) {
    zx::status client_end = service::Connect<sysmem::Allocator>();
    ASSERT_OK(client_end.status_value());
    sysmem_allocator_ = fidl::BindSyncClient(std::move(*client_end));
    sysmem_allocator_->SetDebugClientInfo(
        fidl::StringView::FromExternal(fsl::GetCurrentProcessName() + "-debug-client"),
        fsl::GetCurrentProcessKoid());
  }

  ~StubDisplayController() { current_buffer_collection_->Close(); }
  void ImportVmoImage(ImportVmoImageRequestView request,
                      ImportVmoImageCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void ImportImage(ImportImageRequestView request,
                   ImportImageCompleter::Sync& _completer) override {
    _completer.Reply(ZX_OK, 1);
  }
  void ReleaseImage(ReleaseImageRequestView request,
                    ReleaseImageCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void ImportEvent(ImportEventRequestView request,
                   ImportEventCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void ReleaseEvent(ReleaseEventRequestView request,
                    ReleaseEventCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void CreateLayer(CreateLayerRequestView request,
                   CreateLayerCompleter::Sync& _completer) override {
    _completer.Reply(ZX_OK, 1);
  }

  void DestroyLayer(DestroyLayerRequestView request,
                    DestroyLayerCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void ImportGammaTable(ImportGammaTableRequestView request,
                        ImportGammaTableCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void ReleaseGammaTable(ReleaseGammaTableRequestView request,
                         ReleaseGammaTableCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetDisplayMode(SetDisplayModeRequestView request,
                      SetDisplayModeCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void SetDisplayColorConversion(SetDisplayColorConversionRequestView request,
                                 SetDisplayColorConversionCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetDisplayGammaTable(SetDisplayGammaTableRequestView request,
                            SetDisplayGammaTableCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetDisplayLayers(SetDisplayLayersRequestView request,
                        SetDisplayLayersCompleter::Sync& _completer) override {
    // Ignore
  }

  void SetLayerPrimaryConfig(SetLayerPrimaryConfigRequestView request,
                             SetLayerPrimaryConfigCompleter::Sync& _completer) override {
    // Ignore
  }

  void SetLayerPrimaryPosition(SetLayerPrimaryPositionRequestView request,
                               SetLayerPrimaryPositionCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerPrimaryAlpha(SetLayerPrimaryAlphaRequestView request,
                            SetLayerPrimaryAlphaCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerCursorConfig(SetLayerCursorConfigRequestView request,
                            SetLayerCursorConfigCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerCursorPosition(SetLayerCursorPositionRequestView request,
                              SetLayerCursorPositionCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerColorConfig(SetLayerColorConfigRequestView request,
                           SetLayerColorConfigCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerImage(SetLayerImageRequestView request,
                     SetLayerImageCompleter::Sync& _completer) override {
    // Ignore
  }

  void CheckConfig(CheckConfigRequestView request,
                   CheckConfigCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void ApplyConfig(ApplyConfigRequestView request,
                   ApplyConfigCompleter::Sync& _completer) override {
    // Ignore
  }

  void EnableVsync(EnableVsyncRequestView request,
                   EnableVsyncCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetVirtconMode(SetVirtconModeRequestView request,
                      SetVirtconModeCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void ImportBufferCollection(ImportBufferCollectionRequestView request,
                              ImportBufferCollectionCompleter::Sync& _completer) override {
    zx::status endpoints = fidl::CreateEndpoints<sysmem::BufferCollection>();
    ASSERT_OK(endpoints.status_value());

    ASSERT_TRUE(sysmem_allocator_
                    ->BindSharedCollection(std::move(request->collection_token),
                                           std::move(endpoints->server))
                    .ok());
    current_buffer_collection_ = fidl::BindSyncClient(std::move(endpoints->client));

    _completer.Reply(ZX_OK);
  }
  void ReleaseBufferCollection(ReleaseBufferCollectionRequestView request,
                               ReleaseBufferCollectionCompleter::Sync& _completer) override {}

  void SetBufferCollectionConstraints(
      SetBufferCollectionConstraintsRequestView request,
      SetBufferCollectionConstraintsCompleter::Sync& _completer) override {
    sysmem::wire::BufferCollectionConstraints constraints;
    constraints.usage.cpu = sysmem::wire::kCpuUsageWriteOften | sysmem::wire::kCpuUsageRead;
    constraints.min_buffer_count = 1;
    constraints.image_format_constraints_count = 1;
    auto& image_constraints = constraints.image_format_constraints[0];
    image_constraints = image_format::GetDefaultImageFormatConstraints();
    image_constraints.pixel_format.type = sysmem::wire::PixelFormatType::kBgra32;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value = sysmem::wire::kFormatModifierLinear;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = sysmem::wire::ColorSpaceType::kSrgb;
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

  void GetSingleBufferFramebuffer(GetSingleBufferFramebufferRequestView request,
                                  GetSingleBufferFramebufferCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void IsCaptureSupported(IsCaptureSupportedRequestView request,
                          IsCaptureSupportedCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void ImportImageForCapture(ImportImageForCaptureRequestView request,
                             ImportImageForCaptureCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void StartCapture(StartCaptureRequestView request,
                    StartCaptureCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void ReleaseCapture(ReleaseCaptureRequestView request,
                      ReleaseCaptureCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void AcknowledgeVsync(AcknowledgeVsyncRequestView request,
                        AcknowledgeVsyncCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetMinimumRgb(SetMinimumRgbRequestView request,
                     SetMinimumRgbCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

 private:
  std::optional<fidl::WireSyncClient<sysmem::Allocator>> sysmem_allocator_;
  std::optional<fidl::WireSyncClient<sysmem::BufferCollection>> current_buffer_collection_;
  bool use_ram_domain_;
};

}  // namespace

void TestDisplayStride(bool ram_domain) {
  zx::status endpoints = fidl::CreateEndpoints<fhd::Controller>();
  ASSERT_OK(endpoints.status_value());

  constexpr uint32_t kPixelFormat = ZX_PIXEL_FORMAT_ARGB_8888;
  fhd::wire::Mode mode = {
      .horizontal_resolution = 301,
      .vertical_resolution = 250,
  };

  fidl::WireEventSender<fhd::Controller> event_sender(std::move(endpoints->server));
  {
    uint32_t pixel_format = kPixelFormat;
    fhd::wire::Info info = {
        .modes = fidl::VectorView<fhd::wire::Mode>::FromExternal(&mode, 1),
        .pixel_format = fidl::VectorView<uint32_t>::FromExternal(&pixel_format, 1),
    };
    ASSERT_OK(event_sender.OnDisplaysChanged(
        fidl::VectorView<fhd::wire::Info>::FromExternal(&info, 1), {}));
  }
  StubDisplayController controller(ram_domain);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(event_sender.server_end()),
                                         &controller));

  loop.StartThread();

  const char* error;
  zx_status_t status = fb_bind_with(true, &error, std::move(endpoints->client));
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
