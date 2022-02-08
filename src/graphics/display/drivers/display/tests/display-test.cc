// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.display/cpp/wire.h>
#include <fuchsia/hardware/display/controller/c/banjo.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/wire_messaging.h>
#include <zircon/errors.h>

#include <array>

#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

#include "fidl/fuchsia.hardware.display/cpp/markers.h"
#include "src/graphics/display/drivers/display/client.h"
#include "src/graphics/display/drivers/display/controller.h"
#include "src/graphics/display/drivers/display/util.h"
namespace display {

TEST(DisplayTest, NoOpTest) { EXPECT_OK(ZX_OK); }

TEST(DisplayTest, ClientVSyncOk) {
  constexpr uint64_t kControllerStampValue = 1u;
  constexpr uint64_t kClientStampValue = 2u;

  zx::channel server_chl, client_chl;
  zx_status_t status = zx::channel::create(0, &server_chl, &client_chl);
  EXPECT_OK(status);
  Controller controller(nullptr);
  ClientProxy clientproxy(&controller, false, false, 0, std::move(server_chl));
  clientproxy.EnableVsync(true);
  fbl::AutoLock lock(controller.mtx());
  clientproxy.UpdateConfigStampMapping({
      .controller_stamp = {.value = kControllerStampValue},
      .client_stamp = {.value = kClientStampValue},
  });

  status = clientproxy.OnDisplayVsync(0, 0, {.value = kControllerStampValue});
  EXPECT_OK(status);

  fidl::WireSyncClient<fuchsia_hardware_display::Controller> client(std::move(client_chl));

  class EventHandler : public fidl::WireSyncEventHandler<fuchsia_hardware_display::Controller> {
   public:
    EventHandler(fuchsia_hardware_display::wire::ConfigStamp expected_config_stamp)
        : expected_config_stamp_(expected_config_stamp) {}

    void OnVsync(fidl::WireEvent<fuchsia_hardware_display::Controller::OnVsync>* event) override {
      if (event->applied_config_stamp == expected_config_stamp_) {
        vsync_handled_ = true;
      }
    }

    zx_status_t Unknown() override { return ZX_ERR_NOT_SUPPORTED; }

    bool vsync_handled_ = false;
    fuchsia_hardware_display::wire::ConfigStamp expected_config_stamp_ =
        fuchsia_hardware_display::wire::kInvalidConfigStampFidl;
  };

  EventHandler event_handler({.value = kClientStampValue});
  EXPECT_TRUE(client.HandleOneEvent(event_handler).ok());
  EXPECT_TRUE(event_handler.vsync_handled_);

  clientproxy.CloseTest();
}

TEST(DisplayTest, ClientVSynPeerClosed) {
  zx::channel server_chl, client_chl;
  zx_status_t status = zx::channel::create(0, &server_chl, &client_chl);
  EXPECT_OK(status);
  Controller controller(nullptr);
  ClientProxy clientproxy(&controller, false, false, 0, std::move(server_chl));
  clientproxy.EnableVsync(true);
  fbl::AutoLock lock(controller.mtx());
  client_chl.reset();
  status = clientproxy.OnDisplayVsync(0, 0, INVALID_CONFIG_STAMP_BANJO);
  EXPECT_TRUE(status == ZX_ERR_PEER_CLOSED);
  clientproxy.CloseTest();
}

TEST(DisplayTest, ClientVSyncNotSupported) {
  zx::channel server_chl, client_chl;
  zx_status_t status = zx::channel::create(0, &server_chl, &client_chl);
  EXPECT_OK(status);
  Controller controller(nullptr);
  ClientProxy clientproxy(&controller, false, false, 0, std::move(server_chl));
  fbl::AutoLock lock(controller.mtx());
  status = clientproxy.OnDisplayVsync(0, 0, INVALID_CONFIG_STAMP_BANJO);
  EXPECT_TRUE(status == ZX_ERR_NOT_SUPPORTED);
  clientproxy.CloseTest();
}

TEST(DisplayTest, ClientMustDrainPendingStamps) {
  constexpr size_t kNumPendingStamps = 5;
  constexpr std::array<uint64_t, kNumPendingStamps> kControllerStampValues = {1u, 2u, 3u, 4u, 5u};
  constexpr std::array<uint64_t, kNumPendingStamps> kClientStampValues = {2u, 3u, 4u, 5u, 6u};

  zx::channel server_chl, client_chl;
  zx_status_t status = zx::channel::create(0, &server_chl, &client_chl);
  EXPECT_OK(status);
  Controller controller(nullptr);
  ClientProxy clientproxy(&controller, false, false, 0, std::move(server_chl));
  clientproxy.EnableVsync(false);
  fbl::AutoLock lock(controller.mtx());
  for (size_t i = 0; i < kNumPendingStamps; i++) {
    clientproxy.UpdateConfigStampMapping({
        .controller_stamp = {.value = kControllerStampValues[i]},
        .client_stamp = {.value = kClientStampValues[i]},
    });
  }

  status = clientproxy.OnDisplayVsync(0, 0, {.value = kControllerStampValues.back()});
  EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);

  // Even if Vsync is disabled, ClientProxy should always drain pending
  // controller stamps.
  EXPECT_EQ(clientproxy.pending_applied_config_stamps().size(), 1);
  EXPECT_EQ(clientproxy.pending_applied_config_stamps().front().controller_stamp.value,
            kControllerStampValues.back());

  clientproxy.CloseTest();
}

}  // namespace display
