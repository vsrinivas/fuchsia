// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vc-display.h"

#include <fcntl.h>
#include <fidl/fuchsia.hardware.display/cpp/wire.h>
#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fuchsia/hardware/display/controller/c/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fidl/coding.h>
#include <lib/image-format-llcpp/image-format-llcpp.h>
#include <lib/stdcompat/optional.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <list>
#include <unordered_map>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "vc.h"

namespace fhd = fuchsia_hardware_display;
namespace sysmem = fuchsia_sysmem;

namespace {

// Arbitrary
constexpr uint32_t kSingleBufferStride = 4;

class StubDisplayController : public fidl::WireServer<fhd::Controller> {
 public:
  StubDisplayController() {}

  virtual ~StubDisplayController() {}

  void ImportVmoImage(ImportVmoImageRequestView request,
                      ImportVmoImageCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void ImportImage(ImportImageRequestView request,
                   ImportImageCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void ReleaseImage(ReleaseImageRequestView request,
                    ReleaseImageCompleter::Sync& _completer) override {
    images_.remove_if(
        [image_id = request->image_id](uint64_t image) { return (image == image_id); });
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
    layers_.push_back(next_layer_);
    _completer.Reply(ZX_OK, next_layer_++);
  }

  void DestroyLayer(DestroyLayerRequestView request,
                    DestroyLayerCompleter::Sync& _completer) override {
    layers_.remove_if(
        [layer_id = request->layer_id](uint64_t layer) { return (layer == layer_id); });
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
    // Ignore
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
    _completer.Reply(fhd::wire::ConfigResult::kOk,
                     fidl::VectorView<fhd::wire::ClientCompositionOp>());
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
    EXPECT_TRUE(false);
  }
  void ReleaseBufferCollection(ReleaseBufferCollectionRequestView request,
                               ReleaseBufferCollectionCompleter::Sync& _completer) override {}

  void SetBufferCollectionConstraints(
      SetBufferCollectionConstraintsRequestView request,
      SetBufferCollectionConstraintsCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
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

  void SetMinimumRgb(SetMinimumRgbRequestView request,
                     SetMinimumRgbCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void AcknowledgeVsync(AcknowledgeVsyncRequestView request,
                        AcknowledgeVsyncCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  };

  const std::list<uint64_t>& images() const { return images_; }
  const std::list<uint64_t>& layers() const { return layers_; }

  const std::unordered_map<uint64_t,
                           std::unique_ptr<fidl::WireSyncClient<sysmem::BufferCollection>>>&
  buffer_collections() const {
    return buffer_collections_;
  }

 protected:
  std::list<uint64_t> images_;
  uint64_t next_image_ = 1;
  // We want to make sure that we destroy every layer we create.
  std::list<uint64_t> layers_;
  uint64_t next_layer_ = 1;

  std::unordered_map<uint64_t, std::unique_ptr<fidl::WireSyncClient<sysmem::BufferCollection>>>
      buffer_collections_;
};

class StubSingleBufferDisplayController : public StubDisplayController {
 public:
  void ImportVmoImage(ImportVmoImageRequestView request,
                      ImportVmoImageCompleter::Sync& _completer) override {
    EXPECT_NE(ZX_HANDLE_INVALID, request->vmo.get());
    images_.push_back(next_image_);
    _completer.Reply(ZX_OK, next_image_++);
  }
  void GetSingleBufferFramebuffer(GetSingleBufferFramebufferRequestView request,
                                  GetSingleBufferFramebufferCompleter::Sync& _completer) override {
    zx::vmo vmo;
    zx::vmo::create(4096, 0, &vmo);
    _completer.Reply(ZX_OK, std::move(vmo), kSingleBufferStride);
  }
};

class StubMultiBufferDisplayController : public StubDisplayController {
 public:
  void GetSingleBufferFramebuffer(GetSingleBufferFramebufferRequestView request,
                                  GetSingleBufferFramebufferCompleter::Sync& _completer) override {
    _completer.Reply(ZX_ERR_NOT_SUPPORTED, zx::vmo(), 0);
  }
  void ImportImage(ImportImageRequestView request,
                   ImportImageCompleter::Sync& _completer) override {
    images_.push_back(next_image_);
    _completer.Reply(ZX_OK, next_image_++);
  }

  void ImportBufferCollection(ImportBufferCollectionRequestView request,
                              ImportBufferCollectionCompleter::Sync& _completer) override {
    auto endpoints = fidl::CreateEndpoints<sysmem::BufferCollection>();
    ASSERT_OK(endpoints.status_value());
    ASSERT_OK(get_sysmem_allocator()
                  ->BindSharedCollection(std::move(request->collection_token),
                                         std::move(endpoints->server))
                  .status());
    buffer_collections_[request->collection_id] =
        std::make_unique<fidl::WireSyncClient<sysmem::BufferCollection>>(
            fidl::BindSyncClient(std::move(endpoints->client)));

    _completer.Reply(ZX_OK);
  }
  void ReleaseBufferCollection(ReleaseBufferCollectionRequestView request,
                               ReleaseBufferCollectionCompleter::Sync& _completer) override {
    buffer_collections_.erase(request->collection_id);
  }

  void SetBufferCollectionConstraints(
      SetBufferCollectionConstraintsRequestView request,
      SetBufferCollectionConstraintsCompleter::Sync& _completer) override {
    sysmem::wire::BufferCollectionConstraints constraints;
    constraints.usage.cpu = sysmem::wire::kCpuUsageWriteOften | sysmem::wire::kCpuUsageRead;
    constraints.min_buffer_count = 1;
    constraints.image_format_constraints_count = 2;
    for (uint32_t i = 0; i < 2; i++) {
      auto& image_constraints = constraints.image_format_constraints[i];
      image_constraints = image_format::GetDefaultImageFormatConstraints();
      if (i == 0) {
        image_constraints.pixel_format.type = sysmem::wire::PixelFormatType::kBgra32;
      } else {
        image_constraints.pixel_format.type = sysmem::wire::PixelFormatType::kRgb565;
      }
      image_constraints.pixel_format.has_format_modifier = true;
      image_constraints.pixel_format.format_modifier.value = sysmem::wire::kFormatModifierLinear;
      image_constraints.color_spaces_count = 1;
      image_constraints.color_space[0].type = sysmem::wire::ColorSpaceType::kSrgb;
      image_constraints.max_coded_width = 0xffffffff;
      image_constraints.max_coded_height = 0xffffffff;
      image_constraints.max_bytes_per_row = 0xffffffff;
      image_constraints.bytes_per_row_divisor = 4;
    }

    buffer_collections_[request->collection_id]->SetConstraints(true, constraints);
    _completer.Reply(ZX_OK);
  }
};
}  // namespace

zx_status_t log_create_vc(vc_gfx_t* graphics, vc_t** vc_out) { return ZX_OK; }

void log_delete_vc(vc_t* vc) {}

void set_log_listener_active(bool active) {}

void vc_attach_gfx(vc_t* vc) {}

zx_status_t vc_init_gfx(vc_gfx_t* gfx, zx_handle_t fb_vmo, int32_t width, int32_t height,
                        zx_pixel_format_t format, int32_t stride) {
  return ZX_OK;
}

void vc_change_graphics(vc_gfx_t* graphics) {}

class VcDisplayTest : public zxtest::Test {
  void SetUp() override { ASSERT_TRUE(vc_sysmem_connect()); }

  void TearDown() override {
    if (loop_) {
      // Ensure the loop processes all queued FIDL messages.
      loop_->Quit();
      loop_->JoinThreads();
      loop_->ResetQuit();
      loop_->RunUntilIdle();
    }

    loop_.reset();
    if (controller_) {
      ASSERT_EQ(controller_->layers().size(), 0);
      ASSERT_EQ(controller_->images().size(), 0);
      ASSERT_EQ(controller_->buffer_collections().size(), 0u);
    }
    controller_.reset();
  }

 protected:
  void InitializeServer() {
    auto endpoints = fidl::CreateEndpoints<fhd::Controller>();
    ASSERT_OK(endpoints.status_value());
    initialize_display_channel(std::move(endpoints->client));
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    loop_->StartThread();

    server_binding_ =
        fidl::BindServer(loop_->dispatcher(), std::move(endpoints->server), controller_.get());
  }
  void SendAddDisplay(fhd::wire::Info* display) {
    server_binding_.value()->OnDisplaysChanged(
        fidl::VectorView<fhd::wire::Info>::FromExternal(display, 1), fidl::VectorView<uint64_t>());
  }
  void SendRemoveDisplay(uint64_t id) {
    server_binding_.value()->OnDisplaysChanged(fidl::VectorView<fhd::wire::Info>(),
                                               fidl::VectorView<uint64_t>::FromExternal(&id, 1));
  }

  void ProcessEvent() { ASSERT_OK(dc_callback_handler(ZX_CHANNEL_READABLE)); }

  std::unique_ptr<StubDisplayController> controller_;

  // Loop needs to be torn down before controller, because that causes the
  // binding to close.
  std::unique_ptr<async::Loop> loop_;

  // Server binding reference used to send events.
  cpp17::optional<fidl::ServerBindingRef<fhd::Controller>> server_binding_;
};

TEST_F(VcDisplayTest, EmptyRebind) { ASSERT_EQ(rebind_display(true), ZX_ERR_NO_RESOURCES); }

TEST_F(VcDisplayTest, OneDisplay) {
  controller_ = std::make_unique<StubSingleBufferDisplayController>();
  InitializeServer();

  fhd::wire::Info info;
  info.id = 1;
  uint32_t format = 0x0;
  fhd::wire::Mode mode;
  info.modes = fidl::VectorView<fhd::wire::Mode>::FromExternal(&mode, 1);
  info.pixel_format = fidl::VectorView<uint32_t>::FromExternal(&format, 1);

  SendAddDisplay(&info);
  ProcessEvent();
  ASSERT_TRUE(is_primary_bound());
  display_info_t* primary = list_peek_head_type(get_display_list(), display_info_t, node);
  ASSERT_TRUE(primary->bound);

  handle_display_removed(1);
  ASSERT_FALSE(is_primary_bound());
}

TEST_F(VcDisplayTest, TwoDisplays) {
  controller_ = std::make_unique<StubSingleBufferDisplayController>();
  InitializeServer();

  fhd::wire::Info hardware_display;
  hardware_display.id = 1;
  uint32_t format = 0x0;
  fhd::wire::Mode mode;
  hardware_display.modes = fidl::VectorView<fhd::wire::Mode>::FromExternal(&mode, 1);
  hardware_display.pixel_format = fidl::VectorView<uint32_t>::FromExternal(&format, 1);

  // Add the first display.
  SendAddDisplay(&hardware_display);
  ProcessEvent();
  ASSERT_TRUE(is_primary_bound());

  display_info_t* primary = list_peek_head_type(get_display_list(), display_info_t, node);
  ASSERT_TRUE(primary->bound);

  // Add the second display.
  hardware_display.id = 2;
  SendAddDisplay(&hardware_display);
  ProcessEvent();
  ASSERT_TRUE(is_primary_bound());

  // Check that all of the displays were bound.
  display_info_t* info;
  int num_displays = 0;
  list_for_every_entry (get_display_list(), info, display_info_t, node) {
    ASSERT_TRUE(info->bound);
    num_displays++;
  }
  ASSERT_EQ(num_displays, 2);

  // Remove the second display.
  SendRemoveDisplay(2u);
  ProcessEvent();
  // handle_display_removed(2);
  ASSERT_TRUE(is_primary_bound());

  // Remove the first display.
  SendRemoveDisplay(1u);
  ProcessEvent();
  ASSERT_FALSE(is_primary_bound());
}

// This test checks that the primary display switches over correctly.
// It allocates display 1 and then display 2, then removes display 1.
// Display 2 should switch over to the primary display.
TEST_F(VcDisplayTest, ChangePrimaryDisplay) {
  controller_ = std::make_unique<StubSingleBufferDisplayController>();
  InitializeServer();

  fhd::wire::Info hardware_display;
  hardware_display.id = 1;
  uint32_t format = 0x0;
  fhd::wire::Mode mode;
  hardware_display.modes = fidl::VectorView<fhd::wire::Mode>::FromExternal(&mode, 1);
  hardware_display.pixel_format = fidl::VectorView<uint32_t>::FromExternal(&format, 1);

  // Add the first display.
  SendAddDisplay(&hardware_display);
  ProcessEvent();
  ASSERT_TRUE(is_primary_bound());

  display_info_t* primary = list_peek_head_type(get_display_list(), display_info_t, node);
  ASSERT_TRUE(primary->bound);

  // Add the second display.
  hardware_display.id = 2;
  SendAddDisplay(&hardware_display);
  ProcessEvent();
  ASSERT_TRUE(is_primary_bound());

  // Check that all of the displays were bound.
  display_info_t* info;
  int num_displays = 0;
  list_for_every_entry (get_display_list(), info, display_info_t, node) {
    ASSERT_TRUE(info->bound);
    num_displays++;
  }
  ASSERT_EQ(num_displays, 2);

  // Remove the first display.
  SendRemoveDisplay(1);
  ProcessEvent();
  ASSERT_TRUE(is_primary_bound());

  // Remove the second display.
  SendRemoveDisplay(2);
  ProcessEvent();
  ASSERT_FALSE(is_primary_bound());
}

TEST_F(VcDisplayTest, SingleBufferVmo) {
  controller_ = std::make_unique<StubSingleBufferDisplayController>();
  InitializeServer();

  fhd::wire::Info hardware_display;
  hardware_display.id = 1;
  uint32_t format = 0x0;
  fhd::wire::Mode mode;
  hardware_display.modes = fidl::VectorView<fhd::wire::Mode>::FromExternal(&mode, 1);
  hardware_display.pixel_format = fidl::VectorView<uint32_t>::FromExternal(&format, 1);

  // Add the first display.
  SendAddDisplay(&hardware_display);
  ProcessEvent();
  ASSERT_TRUE(is_primary_bound());

  display_info_t* primary = list_peek_head_type(get_display_list(), display_info_t, node);
  ASSERT_TRUE(primary->bound);
  EXPECT_EQ(1u, controller_->images().size());

  EXPECT_NE(ZX_HANDLE_INVALID, primary->image_vmo);
  EXPECT_EQ(kSingleBufferStride, primary->stride);

  SendRemoveDisplay(1);
  ProcessEvent();
}

class VcDisplayMultibufferTest : public VcDisplayTest {
 public:
  void SetupMode(zx_pixel_format_t format) {
    controller_ = std::make_unique<StubMultiBufferDisplayController>();
    InitializeServer();

    fhd::wire::Info hardware_display;
    hardware_display.id = 1;
    fhd::wire::Mode mode;
    mode.horizontal_resolution = 641;
    mode.vertical_resolution = 480;
    hardware_display.modes = fidl::VectorView<fhd::wire::Mode>::FromExternal(&mode, 1);
    hardware_display.pixel_format = fidl::VectorView<uint32_t>::FromExternal(&format, 1);

    // Add the first display.
    SendAddDisplay(&hardware_display);
    ProcessEvent();
    ASSERT_TRUE(is_primary_bound());
  }

  void TeardownDisplay() {
    SendRemoveDisplay(1);
    ProcessEvent();
  }
};

TEST_F(VcDisplayMultibufferTest, RGBA32) {
  SetupMode(ZX_PIXEL_FORMAT_ARGB_8888);
  display_info_t* primary = list_peek_head_type(get_display_list(), display_info_t, node);
  ASSERT_TRUE(primary->bound);
  EXPECT_EQ(primary->format, ZX_PIXEL_FORMAT_ARGB_8888);

  EXPECT_NE(ZX_HANDLE_INVALID, primary->image_vmo);
  EXPECT_EQ(641, primary->stride);
  TeardownDisplay();
}

TEST_F(VcDisplayMultibufferTest, RGB565) {
  SetupMode(ZX_PIXEL_FORMAT_RGB_565);
  display_info_t* primary = list_peek_head_type(get_display_list(), display_info_t, node);
  ASSERT_TRUE(primary->bound);

  EXPECT_EQ(primary->format, ZX_PIXEL_FORMAT_RGB_565);

  EXPECT_NE(ZX_HANDLE_INVALID, primary->image_vmo);
  // Stride should be rounded up to be a multiple of 4 bytes.
  EXPECT_EQ(642, primary->stride);
  TeardownDisplay();
}
