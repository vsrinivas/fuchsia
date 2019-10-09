// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/pixelformat.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <algorithm>

#include <ddk/protocol/display/controller.h>
#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "lib/fidl/llcpp/vector_view.h"
#include "lib/zx/event.h"
#include "zircon/errors.h"
#include "zircon/fidl.h"
#include "zircon/time.h"

namespace fhd = ::llcpp::fuchsia::hardware::display;

namespace {

class CoreDisplayTest : public zxtest::Test {
 public:
  void SetUp() override;
  std::unique_ptr<fhd::Controller::SyncClient> dc_client_;

 private:
  zx::channel device_client_channel_;
  zx::channel dc_client_channel_;
  fzl::FdioCaller caller_;
  fbl::Vector<fhd::Info> displays_;
};

void CoreDisplayTest::SetUp() {
  zx::channel device_server_channel;
  fbl::unique_fd fd(open("/dev/class/display-controller/000", O_RDWR));
  zx_status_t status = zx::channel::create(0, &device_server_channel, &device_client_channel_);
  EXPECT_EQ(status, ZX_OK);

  zx::channel dc_server_channel;
  status = zx::channel::create(0, &dc_server_channel, &dc_client_channel_);
  EXPECT_EQ(status, ZX_OK);

  caller_.reset(std::move(fd));
  auto open_status = fhd::Provider::Call::OpenController(
      caller_.channel(), std::move(device_server_channel), std::move(dc_server_channel));
  EXPECT_TRUE(open_status.ok());
  EXPECT_EQ(ZX_OK, open_status.value().s);

  dc_client_ = std::make_unique<fhd::Controller::SyncClient>(std::move(dc_client_channel_));

  bool has_display = false;
  fbl::Vector<fhd::Info> displays_tmp;

  do {
    status = dc_client_->HandleEvents(fhd::Controller::EventHandlers{
        .displays_changed =
            [&displays_tmp, &has_display](fidl::VectorView<fhd::Info> added,
                                          fidl::VectorView<uint64_t> removed) {
              for (unsigned i = 0; i < added.count(); i++) {
                displays_tmp.push_back(added[i]);
              }
              has_display = true;
              return ZX_OK;
            },
        .vsync = [](uint64_t display_id, uint64_t timestamp,
                    fidl::VectorView<uint64_t> images) { return ZX_ERR_NEXT; },
        .client_ownership_change = [](bool has_ownership) { return ZX_ERR_NEXT; },
        .unknown = []() { return ZX_ERR_STOP; }});
    ASSERT_FALSE(status != ZX_OK && status != ZX_ERR_NEXT);
  } while (!has_display);
}

TEST_F(CoreDisplayTest, CoreDisplayAlreadyBoundTest) {
  // Setup connects to display controller. Make sure we can't bound again
  fbl::unique_fd fd(open("/dev/class/display-controller/000", O_RDWR));
  // EXPECT_GE(fd, 0);
  zx::channel device_server, device_client;
  zx_status_t status = zx::channel::create(0, &device_server, &device_client);
  EXPECT_EQ(status, ZX_OK);

  zx::channel dc_server, dc_client;
  status = zx::channel::create(0, &dc_server, &dc_client);
  EXPECT_EQ(status, ZX_OK);

  fzl::FdioCaller caller(std::move(fd));
  auto open_status = fhd::Provider::Call::OpenController(caller.channel(), std::move(device_server),
                                                         std::move(dc_server));
  EXPECT_TRUE(open_status.ok());
  EXPECT_EQ(ZX_ERR_ALREADY_BOUND, open_status.value().s);
}

// This test assumes we have a valid VMO. Let's create this using the
// allocate VMO function.
TEST_F(CoreDisplayTest, ImportVmoImage) {
  auto alloc_status = dc_client_->AllocateVmo(1024 * 600 * 4);
  EXPECT_TRUE(alloc_status.ok());
  EXPECT_EQ(ZX_OK, alloc_status.value().res);

  fhd::ImageConfig image_config = {};
  image_config.type = IMAGE_TYPE_SIMPLE;
  image_config.pixel_format = ZX_PIXEL_FORMAT_RGB_x888;
  zx::vmo local_vmo;
  auto import_image_status =
      dc_client_->ImportVmoImage(image_config, std::move(alloc_status.value().vmo), 0);

  EXPECT_TRUE(import_image_status.ok());
  EXPECT_EQ(ZX_OK, import_image_status.value().res);
}

TEST_F(CoreDisplayTest, CreateLayer) {
  auto resp = dc_client_->CreateLayer();
  EXPECT_TRUE(resp.ok());
  EXPECT_EQ(ZX_OK, resp.value().res);
  EXPECT_EQ(1, resp.value().layer_id);
}

TEST_F(CoreDisplayTest, CreateLayerNoResource) {
  for (int i = 0; i < 65536; i++) {
    auto resp = dc_client_->CreateLayer();
    EXPECT_TRUE(resp.ok());
    EXPECT_EQ(ZX_OK, resp.value().res);
    EXPECT_EQ(i + 1, resp.value().layer_id);
  }

  auto resp = dc_client_->CreateLayer();
  EXPECT_TRUE(resp.ok());
  EXPECT_EQ(ZX_ERR_NO_RESOURCES, resp.value().res);
}

#if 0
TEST_F(CoreDisplayTest, DestroyLayer) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetDisplayMode) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetDisplayColorConversion) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetDisplayLayers) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerPrimaryConfig) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerPrimaryPosition) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerPrimaryAlpha) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerCursorConfig) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerCursorPosition) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerColorConfig) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetLayerImage) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, CheckConfig) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, ApplyConfig) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, EnableVsync) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetVirtconMode) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, ComputeLinearImageStride) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, ImportBufferCollection) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, ReleaseBufferCollection) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, SetBufferCollectionConstraints) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, ImportImage) { EXPECT_OK(ZX_OK); }

TEST_F(CoreDisplayTest, ReleaseImage) { EXPECT_OK(ZX_OK); }

// There is no return for this defined. Cannot verify if it actually
// worked or not
TEST_F(CoreDisplayTest, ImportEvent) { EXPECT_OK(ZX_OK); }

// There is no return for this defined. Cannot verify if it actually
// worked or not
TEST_F(CoreDisplayTest, ReleaseEvent) { EXPECT_OK(ZX_OK); }
#endif

}  // namespace

int main(int argc, char** argv) {
  constexpr char kDriverPath[] = "/dev/display/fake-display";
  if (access(kDriverPath, F_OK) != -1) {
    zxtest::RunAllTests(argc, argv);
  }
  return 0;
}
