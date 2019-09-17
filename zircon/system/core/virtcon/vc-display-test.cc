// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vc-display.h"

#include <fcntl.h>
#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/coding.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/device/display-controller.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <list>

#include <fbl/unique_fd.h>
#include <port/port.h>
#include <zxtest/zxtest.h>

#include "vc.h"

namespace fhd = ::llcpp::fuchsia::hardware::display;

namespace {

// Arbitrary
constexpr uint32_t kSingleBufferStride = 4;
constexpr uint32_t kMultiBufferStride = 8;

class StubDisplayController : public fhd::Controller::Interface {
 public:
  StubDisplayController() {}

  virtual ~StubDisplayController() {}

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

  void ComputeLinearImageStride(uint32_t width, uint32_t pixel_format,
                                ComputeLinearImageStrideCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void AllocateVmo(uint64_t size, AllocateVmoCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void ImportBufferCollection(uint64_t collection_id, ::zx::channel collection_token,
                              ImportBufferCollectionCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void ReleaseBufferCollection(uint64_t collection_id,
                               ReleaseBufferCollectionCompleter::Sync _completer) override {}

  void SetBufferCollectionConstraints(
      uint64_t collection_id, fhd::ImageConfig config,
      SetBufferCollectionConstraintsCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void GetSingleBufferFramebuffer(GetSingleBufferFramebufferCompleter::Sync _completer) override {
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
};

class StubSingleBufferDisplayController : public StubDisplayController {
 public:
  void GetSingleBufferFramebuffer(GetSingleBufferFramebufferCompleter::Sync _completer) override {
    zx::vmo vmo;
    zx::vmo::create(4096, 0, &vmo);
    _completer.Reply(ZX_OK, std::move(vmo), kSingleBufferStride);
  }
};

class StubMultiBufferDisplayController : public StubDisplayController {
 public:
  void GetSingleBufferFramebuffer(GetSingleBufferFramebufferCompleter::Sync _completer) override {
    _completer.Reply(ZX_ERR_NOT_SUPPORTED, zx::vmo(), 0);
  }

  void ComputeLinearImageStride(uint32_t width, uint32_t pixel_format,
                                ComputeLinearImageStrideCompleter::Sync _completer) override {
    _completer.Reply(kMultiBufferStride);
  }

  void AllocateVmo(uint64_t size, AllocateVmoCompleter::Sync _completer) override {
    zx::vmo vmo;
    zx::vmo::create(size, 0, &vmo);
    _completer.Reply(ZX_OK, std::move(vmo));
  }
};
}  // namespace

port_t port;

zx_status_t log_create_vc(vc_gfx_t* graphics, vc_t** vc_out) { return ZX_OK; }

void log_delete_vc(vc_t* vc) {}

void set_log_listener_active(bool active) {}

void vc_attach_gfx(vc_t* vc) {}

// We want to make sure that we destroy every layer we create.
std::list<uint64_t> layers;
uint64_t next_layer = 1;
zx_status_t create_layer(uint64_t display_id, uint64_t* layer_id) {
  layers.push_back(next_layer);
  *layer_id = next_layer++;
  return ZX_OK;
}

void destroy_layer(uint64_t layer_id) {
  layers.remove_if([layer_id](uint64_t layer) { return (layer == layer_id); });
}

// We want to make sure we release every image we create.
std::list<uint64_t> images;
uint64_t next_image = 1;
zx_status_t import_vmo(zx_handle_t vmo, fhd::ImageConfig* config, uint64_t* id) {
  images.push_back(next_image);
  *id = next_image++;
  return ZX_OK;
}

void release_image(uint64_t image_id) {
  images.remove_if([image_id](uint64_t image) { return (image == image_id); });
}

zx_status_t set_display_layer(uint64_t display_id, uint64_t layer_id) { return ZX_OK; }

zx_status_t configure_layer(display_info_t* display, uint64_t layer_id, uint64_t image_id,
                            fhd::ImageConfig* config) {
  return ZX_OK;
}

zx_status_t apply_configuration() { return ZX_OK; }

zx_status_t vc_init_gfx(vc_gfx_t* gfx, zx_handle_t fb_vmo, int32_t width, int32_t height,
                        zx_pixel_format_t format, int32_t stride) {
  return ZX_OK;
}

void vc_change_graphics(vc_gfx_t* graphics) {}

class VcDisplayTest : public zxtest::Test {
  void SetUp() override {
    layers.clear();
    next_layer = 1;

    images.clear();
    next_image = 1;
  }

  void TearDown() override {
    ASSERT_EQ(layers.size(), 0);
    ASSERT_EQ(images.size(), 0);
    loop_.reset();
    controller_.reset();
  }

 protected:
  void InitializeServer() {
    zx::channel client_end, server_end;
    zx::channel::create(0u, &server_end, &client_end);
    initialize_display_channel(std::move(client_end));
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
    loop_->StartThread();

    server_end_ = zx::unowned_channel(server_end);
    ASSERT_OK(fidl::Bind(loop_->dispatcher(), std::move(server_end), controller_.get()));
  }
  void SendAddDisplay(fhd::Info* display) {
    fhd::Controller::SendDisplaysChangedEvent(zx::unowned_channel(server_end_),
                                              fidl::VectorView(display, 1),
                                              fidl::VectorView<uint64_t>());
  }
  void SendRemoveDisplay(uint64_t id) {
    fhd::Controller::SendDisplaysChangedEvent(zx::unowned_channel(server_end_),
                                              fidl::VectorView<fhd::Info>(),
                                              fidl::VectorView<uint64_t>(&id, 1));
  }
  void ProcessEvent() { ASSERT_OK(dc_callback_handler(nullptr, ZX_CHANNEL_READABLE, 0)); }
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
  info.modes = fidl::VectorView(&mode, 1);
  info.pixel_format = fidl::VectorView(&format, 1);

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
  hardware_display.modes = fidl::VectorView(&mode, 1);
  hardware_display.pixel_format = fidl::VectorView(&format, 1);

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
  hardware_display.modes = fidl::VectorView(&mode, 1);
  hardware_display.pixel_format = fidl::VectorView(&format, 1);

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
  hardware_display.modes = fidl::VectorView(&mode, 1);
  hardware_display.pixel_format = fidl::VectorView(&format, 1);

  // Add the first display.
  SendAddDisplay(&hardware_display);
  ProcessEvent();
  ASSERT_TRUE(is_primary_bound());

  display_info_t* primary = list_peek_head_type(get_display_list(), display_info_t, node);
  ASSERT_TRUE(primary->bound);

  EXPECT_NE(ZX_HANDLE_INVALID, primary->image_vmo);
  EXPECT_EQ(kSingleBufferStride, primary->stride);

  SendRemoveDisplay(1);
  ProcessEvent();
}

TEST_F(VcDisplayTest, MultiBufferVmo) {
  controller_ = std::make_unique<StubMultiBufferDisplayController>();
  InitializeServer();

  fhd::Info hardware_display;
  hardware_display.id = 1;
  uint32_t format = 0x0;
  fhd::Mode mode;
  hardware_display.modes = fidl::VectorView(&mode, 1);
  hardware_display.pixel_format = fidl::VectorView(&format, 1);

  // Add the first display.
  SendAddDisplay(&hardware_display);
  ProcessEvent();
  ASSERT_TRUE(is_primary_bound());

  display_info_t* primary = list_peek_head_type(get_display_list(), display_info_t, node);
  ASSERT_TRUE(primary->bound);

  EXPECT_NE(ZX_HANDLE_INVALID, primary->image_vmo);
  EXPECT_EQ(kMultiBufferStride, primary->stride);

  SendRemoveDisplay(1);
  ProcessEvent();
}
