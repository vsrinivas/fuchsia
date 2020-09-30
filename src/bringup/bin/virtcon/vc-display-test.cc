// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vc-display.h"

#include <fcntl.h>
#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/coding.h>
#include <lib/image-format-llcpp/image-format-llcpp.h>
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

#include <ddk/protocol/display/controller.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "vc.h"

namespace fhd = ::llcpp::fuchsia::hardware::display;
namespace sysmem = ::llcpp::fuchsia::sysmem;

namespace {

// Arbitrary
constexpr uint32_t kSingleBufferStride = 4;

class StubDisplayController : public fhd::Controller::Interface {
 public:
  StubDisplayController() {}

  virtual ~StubDisplayController() {}

  void ImportVmoImage(fhd::ImageConfig image_config, ::zx::vmo vmo, int32_t offset,
                      ImportVmoImageCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void ImportImage(fhd::ImageConfig image_config, uint64_t collection_id, uint32_t index,
                   ImportImageCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void ReleaseImage(uint64_t image_id, ReleaseImageCompleter::Sync& _completer) override {
    images_.remove_if([image_id](uint64_t image) { return (image == image_id); });
  }
  void ImportEvent(::zx::event event, uint64_t id,
                   ImportEventCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void ReleaseEvent(uint64_t id, ReleaseEventCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void CreateLayer(CreateLayerCompleter::Sync& _completer) override {
    layers_.push_back(next_layer_);
    _completer.Reply(ZX_OK, next_layer_++);
  }

  void DestroyLayer(uint64_t layer_id, DestroyLayerCompleter::Sync& _completer) override {
    layers_.remove_if([layer_id](uint64_t layer) { return (layer == layer_id); });
  }

  void ImportGammaTable(uint64_t gamma_table_id, ::fidl::Array<float, 256> r,
                        ::fidl::Array<float, 256> g, ::fidl::Array<float, 256> b,
                        ImportGammaTableCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void ReleaseGammaTable(uint64_t gamma_table_id,
                         ReleaseGammaTableCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetDisplayMode(uint64_t display_id, fhd::Mode mode,
                      SetDisplayModeCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void SetDisplayColorConversion(uint64_t display_id, ::fidl::Array<float, 3> preoffsets,
                                 ::fidl::Array<float, 9> coefficients,
                                 ::fidl::Array<float, 3> postoffsets,
                                 SetDisplayColorConversionCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetDisplayGammaTable(uint64_t display_id, uint64_t gamma_table_id,
                            SetDisplayGammaTableCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetDisplayLayers(uint64_t display_id, ::fidl::VectorView<uint64_t> layer_ids,
                        SetDisplayLayersCompleter::Sync& _completer) override {
    // Ignore
  }

  void SetLayerPrimaryConfig(uint64_t layer_id, fhd::ImageConfig image_config,
                             SetLayerPrimaryConfigCompleter::Sync& _completer) override {
    // Ignore
  }

  void SetLayerPrimaryPosition(uint64_t layer_id, fhd::Transform transform, fhd::Frame src_frame,
                               fhd::Frame dest_frame,
                               SetLayerPrimaryPositionCompleter::Sync& _completer) override {
    // Ignore
  }

  void SetLayerPrimaryAlpha(uint64_t layer_id, fhd::AlphaMode mode, float val,
                            SetLayerPrimaryAlphaCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerCursorConfig(uint64_t layer_id, fhd::ImageConfig image_config,
                            SetLayerCursorConfigCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerCursorPosition(uint64_t layer_id, int32_t x, int32_t y,
                              SetLayerCursorPositionCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerColorConfig(uint64_t layer_id, uint32_t pixel_format,
                           ::fidl::VectorView<uint8_t> color_bytes,
                           SetLayerColorConfigCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetLayerImage(uint64_t layer_id, uint64_t image_id, uint64_t wait_event_id,
                     uint64_t signal_event_id, SetLayerImageCompleter::Sync& _completer) override {
    // Ignore
  }

  void CheckConfig(bool discard, CheckConfigCompleter::Sync& _completer) override {
    _completer.Reply(fhd::ConfigResult::OK, fidl::VectorView<fhd::ClientCompositionOp>());
  }

  void ApplyConfig(ApplyConfigCompleter::Sync& _completer) override {
    // Ignore
  }

  void EnableVsync(bool enable, EnableVsyncCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetVirtconMode(uint8_t mode, SetVirtconModeCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void ImportBufferCollection(uint64_t collection_id, ::zx::channel collection_token,
                              ImportBufferCollectionCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }
  void ReleaseBufferCollection(uint64_t collection_id,
                               ReleaseBufferCollectionCompleter::Sync& _completer) override {}

  void SetBufferCollectionConstraints(
      uint64_t collection_id, fhd::ImageConfig config,
      SetBufferCollectionConstraintsCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void GetSingleBufferFramebuffer(GetSingleBufferFramebufferCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void IsCaptureSupported(IsCaptureSupportedCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void ImportImageForCapture(fhd::ImageConfig image_config, uint64_t collection_id, uint32_t index,
                             ImportImageForCaptureCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void StartCapture(uint64_t signal_event_id, uint64_t image_id,
                    StartCaptureCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void ReleaseCapture(uint64_t image_id, ReleaseCaptureCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void SetMinimumRgb(uint8_t minimum_rgb, SetMinimumRgbCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  }

  void AcknowledgeVsync(uint64_t cookie, AcknowledgeVsyncCompleter::Sync& _completer) override {
    EXPECT_TRUE(false);
  };

  const std::list<uint64_t>& images() const { return images_; }
  const std::list<uint64_t>& layers() const { return layers_; }

  const std::unordered_map<uint64_t, std::unique_ptr<sysmem::BufferCollection::SyncClient>>&
  buffer_collections() const {
    return buffer_collections_;
  }

 protected:
  std::list<uint64_t> images_;
  uint64_t next_image_ = 1;
  // We want to make sure that we destroy every layer we create.
  std::list<uint64_t> layers_;
  uint64_t next_layer_ = 1;

  std::unordered_map<uint64_t, std::unique_ptr<sysmem::BufferCollection::SyncClient>>
      buffer_collections_;
};

class StubSingleBufferDisplayController : public StubDisplayController {
 public:
  void ImportVmoImage(fhd::ImageConfig image_config, ::zx::vmo vmo, int32_t offset,
                      ImportVmoImageCompleter::Sync& _completer) override {
    EXPECT_NE(ZX_HANDLE_INVALID, vmo.get());
    images_.push_back(next_image_);
    _completer.Reply(ZX_OK, next_image_++);
  }
  void GetSingleBufferFramebuffer(GetSingleBufferFramebufferCompleter::Sync& _completer) override {
    zx::vmo vmo;
    zx::vmo::create(4096, 0, &vmo);
    _completer.Reply(ZX_OK, std::move(vmo), kSingleBufferStride);
  }
};

class StubMultiBufferDisplayController : public StubDisplayController {
 public:
  void GetSingleBufferFramebuffer(GetSingleBufferFramebufferCompleter::Sync& _completer) override {
    _completer.Reply(ZX_ERR_NOT_SUPPORTED, zx::vmo(), 0);
  }
  void ImportImage(fhd::ImageConfig image_config, uint64_t collection_id, uint32_t index,
                   ImportImageCompleter::Sync& _completer) override {
    images_.push_back(next_image_);
    _completer.Reply(ZX_OK, next_image_++);
  }

  void ImportBufferCollection(uint64_t collection_id, ::zx::channel collection_token,
                              ImportBufferCollectionCompleter::Sync& _completer) override {
    zx::channel server, client;
    ASSERT_OK(zx::channel::create(0, &server, &client));
    ASSERT_OK(get_sysmem_allocator()
                  ->BindSharedCollection(std::move(collection_token), std::move(server))
                  .status());
    buffer_collections_[collection_id] =
        std::make_unique<sysmem::BufferCollection::SyncClient>(std::move(client));

    _completer.Reply(ZX_OK);
  }
  void ReleaseBufferCollection(uint64_t collection_id,
                               ReleaseBufferCollectionCompleter::Sync& _completer) override {
    buffer_collections_.erase(collection_id);
  }

  void SetBufferCollectionConstraints(
      uint64_t collection_id, fhd::ImageConfig config,
      SetBufferCollectionConstraintsCompleter::Sync& _completer) override {
    sysmem::BufferCollectionConstraints constraints;
    constraints.usage.cpu = sysmem::cpuUsageWriteOften | sysmem::cpuUsageRead;
    constraints.min_buffer_count = 1;
    constraints.image_format_constraints_count = 2;
    for (uint32_t i = 0; i < 2; i++) {
      auto& image_constraints = constraints.image_format_constraints[i];
      image_constraints = image_format::GetDefaultImageFormatConstraints();
      if (i == 0) {
        image_constraints.pixel_format.type = sysmem::PixelFormatType::BGRA32;
      } else {
        image_constraints.pixel_format.type = sysmem::PixelFormatType::RGB565;
      }
      image_constraints.pixel_format.has_format_modifier = true;
      image_constraints.pixel_format.format_modifier.value = sysmem::FORMAT_MODIFIER_LINEAR;
      image_constraints.color_spaces_count = 1;
      image_constraints.color_space[0].type = sysmem::ColorSpaceType::SRGB;
      image_constraints.max_coded_width = 0xffffffff;
      image_constraints.max_coded_height = 0xffffffff;
      image_constraints.max_bytes_per_row = 0xffffffff;
      image_constraints.bytes_per_row_divisor = 4;
    }

    buffer_collections_[collection_id]->SetConstraints(true, constraints);
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
    zx::channel client_end, server_end;
    zx::channel::create(0u, &server_end, &client_end);
    initialize_display_channel(std::move(client_end));
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    loop_->StartThread();

    server_end_ = zx::unowned_channel(server_end);
    ASSERT_OK(fidl::BindSingleInFlightOnly(loop_->dispatcher(), std::move(server_end),
                                           controller_.get()));
  }
  void SendAddDisplay(fhd::Info* display) {
    fhd::Controller::SendOnDisplaysChangedEvent(zx::unowned_channel(server_end_),
                                                fidl::VectorView(fidl::unowned_ptr(display), 1),
                                                fidl::VectorView<uint64_t>());
  }
  void SendRemoveDisplay(uint64_t id) {
    fhd::Controller::SendOnDisplaysChangedEvent(
        zx::unowned_channel(server_end_), fidl::VectorView<fhd::Info>(),
        fidl::VectorView<uint64_t>(fidl::unowned_ptr(&id), 1));
  }

  void ProcessEvent() { ASSERT_OK(dc_callback_handler(ZX_CHANNEL_READABLE)); }
  std::unique_ptr<StubDisplayController> controller_;
  // Loop needs to be torn down before controller, because that causes the
  // binding to close.
  std::unique_ptr<async::Loop> loop_;
  zx::unowned_channel server_end_;
};

TEST_F(VcDisplayTest, EmptyRebind) { ASSERT_EQ(rebind_display(true), ZX_ERR_NO_RESOURCES); }

TEST_F(VcDisplayTest, OneDisplay) {
  controller_ = std::make_unique<StubSingleBufferDisplayController>();
  InitializeServer();

  fhd::Info info;
  info.id = 1;
  uint32_t format = 0x0;
  fhd::Mode mode;
  info.modes = fidl::VectorView(fidl::unowned_ptr(&mode), 1);
  info.pixel_format = fidl::VectorView(fidl::unowned_ptr(&format), 1);

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

  fhd::Info hardware_display;
  hardware_display.id = 1;
  uint32_t format = 0x0;
  fhd::Mode mode;
  hardware_display.modes = fidl::VectorView(fidl::unowned_ptr(&mode), 1);
  hardware_display.pixel_format = fidl::VectorView(fidl::unowned_ptr(&format), 1);

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

  fhd::Info hardware_display;
  hardware_display.id = 1;
  uint32_t format = 0x0;
  fhd::Mode mode;
  hardware_display.modes = fidl::VectorView(fidl::unowned_ptr(&mode), 1);
  hardware_display.pixel_format = fidl::VectorView(fidl::unowned_ptr(&format), 1);

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

  fhd::Info hardware_display;
  hardware_display.id = 1;
  uint32_t format = 0x0;
  fhd::Mode mode;
  hardware_display.modes = fidl::VectorView(fidl::unowned_ptr(&mode), 1);
  hardware_display.pixel_format = fidl::VectorView(fidl::unowned_ptr(&format), 1);

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

    fhd::Info hardware_display;
    hardware_display.id = 1;
    fhd::Mode mode;
    mode.horizontal_resolution = 641;
    mode.vertical_resolution = 480;
    hardware_display.modes = fidl::VectorView(fidl::unowned_ptr(&mode), 1);
    hardware_display.pixel_format = fidl::VectorView(fidl::unowned_ptr(&format), 1);

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
