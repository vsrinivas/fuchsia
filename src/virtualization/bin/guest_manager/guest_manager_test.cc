// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_manager/guest_manager.h"

#include <lib/sys/cpp/testing/component_context_provider.h>

#include <test/placeholders/cpp/fidl.h>

#include "fuchsia/virtualization/cpp/fidl.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace {

using ::fuchsia::virtualization::GuestDescriptor;

class GuestManagerTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override { TestLoopFixture::SetUp(); }

  sys::testing::ComponentContextProvider provider_;
};

TEST_F(GuestManagerTest, LaunchFailInvalidPath) {
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "invalid_path.cfg");
  bool launch_callback_called = false;
  manager.LaunchGuest({}, {}, [&launch_callback_called](auto res) {
    ASSERT_TRUE(res.is_err());
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, res.err());
    launch_callback_called = true;
  });
  ASSERT_TRUE(launch_callback_called);
}

TEST_F(GuestManagerTest, LaunchFailInvalidConfig) {
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/",
                       "data/configs/bad_schema_invalid_field.cfg");
  bool launch_callback_called = false;
  manager.LaunchGuest({}, {}, [&launch_callback_called](auto res) {
    ASSERT_TRUE(res.is_err());
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, res.err());
    launch_callback_called = true;
  });
  ASSERT_TRUE(launch_callback_called);
}

TEST_F(GuestManagerTest, LaunchAndApplyUserGuestConfig) {
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg");
  fuchsia::virtualization::GuestConfig user_guest_config;
  user_guest_config.mutable_cmdline_add()->emplace_back("extra_cmd_line_arg=0");
  user_guest_config.mutable_block_devices()->push_back({
      "data/configs/valid_guest.cfg",
      fuchsia::virtualization::BlockMode::READ_ONLY,
      fuchsia::virtualization::BlockFormat::FILE,
      {},
  });
  fuchsia::virtualization::GuestPtr guest;
  bool launch_callback_called = false;
  manager.LaunchGuest(std::move(user_guest_config), guest.NewRequest(),
                      [&launch_callback_called](auto res) {
                        ASSERT_FALSE(res.is_err());
                        launch_callback_called = true;
                      });
  ASSERT_TRUE(launch_callback_called);
  bool get_callback_called = false;
  manager.Get([&get_callback_called](fuchsia::virtualization::GuestConfig config) {
    const fuchsia::virtualization::BlockSpec& spec0 = config.block_devices()[0];
    ASSERT_EQ("data", spec0.id);
    ASSERT_EQ(fuchsia::virtualization::BlockFormat::FILE, spec0.format);

    const fuchsia::virtualization::BlockSpec& spec1 = config.block_devices()[1];
    ASSERT_EQ("data/configs/valid_guest.cfg", spec1.id);
    ASSERT_EQ(fuchsia::virtualization::BlockFormat::FILE, spec1.format);

    ASSERT_EQ(2u, config.block_devices().size());

    ASSERT_EQ("test cmdline extra_cmd_line_arg=0", config.cmdline());

    ASSERT_EQ(fuchsia::virtualization::KernelType::ZIRCON, config.kernel_type());
    ASSERT_TRUE(config.kernel());
    ASSERT_TRUE(config.ramdisk());
    ASSERT_EQ(4u, config.cpus());
    get_callback_called = true;
  });
  ASSERT_TRUE(get_callback_called);
}

TEST_F(GuestManagerTest, DoubleLaunchFail) {
  bool launch_callback_called = false;
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg");
  fuchsia::virtualization::GuestConfig user_guest_config;
  fuchsia::virtualization::GuestPtr guest;
  manager.LaunchGuest(std::move(user_guest_config), guest.NewRequest(),
                      [&launch_callback_called](auto res) {
                        ASSERT_FALSE(res.is_err());
                        launch_callback_called = true;
                      });
  ASSERT_TRUE(launch_callback_called);

  launch_callback_called = false;
  manager.LaunchGuest(std::move(user_guest_config), guest.NewRequest(),
                      [&launch_callback_called](auto res) {
                        ASSERT_TRUE(res.is_err());
                        ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, res.err());
                        launch_callback_called = true;
                      });
  ASSERT_TRUE(launch_callback_called);
}

TEST_F(GuestManagerTest, LaunchAndGetInfo) {
  bool get_callback_called = false;
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg");

  manager.GetGuestInfo([&get_callback_called](auto info) {
    ASSERT_EQ(info.guest_status(), fuchsia::virtualization::GuestStatus::NOT_STARTED);
    ASSERT_FALSE(info.has_uptime());
    ASSERT_FALSE(info.has_guest_descriptor());
    ASSERT_FALSE(info.has_stop_error());

    get_callback_called = true;
  });
  ASSERT_TRUE(get_callback_called);

  bool launch_callback_called = false;
  fuchsia::virtualization::GuestConfig user_guest_config;
  fuchsia::virtualization::GuestPtr guest;
  manager.LaunchGuest(std::move(user_guest_config), guest.NewRequest(),
                      [&launch_callback_called](auto res) {
                        ASSERT_FALSE(res.is_err());
                        launch_callback_called = true;
                      });
  ASSERT_TRUE(launch_callback_called);

  // TODO(fxbug.dev/104989): Capture config from LaunchGuest once this FIDL protocol is removed.
  fuchsia::virtualization::GuestConfig finalized_config;
  get_callback_called = false;
  manager.Get(
      [&finalized_config, &get_callback_called](fuchsia::virtualization::GuestConfig config) {
        finalized_config = std::move(config);
        get_callback_called = true;
      });
  ASSERT_TRUE(get_callback_called);

  get_callback_called = false;
  manager.GetGuestInfo([&finalized_config, &get_callback_called](auto info) {
    ASSERT_EQ(info.guest_status(), fuchsia::virtualization::GuestStatus::RUNNING);
    ASSERT_GT(info.uptime(), 0);
    ASSERT_FALSE(info.has_stop_error());

    const GuestDescriptor& guest_descriptor = info.guest_descriptor();
    ASSERT_EQ(guest_descriptor.guest_memory(), finalized_config.guest_memory());
    ASSERT_EQ(guest_descriptor.num_cpus(), finalized_config.cpus());

    ASSERT_EQ(guest_descriptor.wayland(), finalized_config.has_wayland_device());
    ASSERT_EQ(guest_descriptor.magma(), finalized_config.has_magma_device());

    ASSERT_EQ(guest_descriptor.network(),
              finalized_config.has_default_net() && finalized_config.default_net());
    ASSERT_EQ(guest_descriptor.balloon(),
              finalized_config.has_virtio_balloon() && finalized_config.virtio_balloon());
    ASSERT_EQ(guest_descriptor.console(),
              finalized_config.has_virtio_console() && finalized_config.virtio_console());
    ASSERT_EQ(guest_descriptor.gpu(),
              finalized_config.has_virtio_gpu() && finalized_config.virtio_gpu());
    ASSERT_EQ(guest_descriptor.rng(),
              finalized_config.has_virtio_rng() && finalized_config.virtio_rng());
    ASSERT_EQ(guest_descriptor.vsock(),
              finalized_config.has_virtio_vsock() && finalized_config.virtio_vsock());
    ASSERT_EQ(guest_descriptor.sound(),
              finalized_config.has_virtio_sound() && finalized_config.virtio_sound());

    get_callback_called = true;
  });
  ASSERT_TRUE(get_callback_called);
}

TEST_F(GuestManagerTest, ConnectToGuest) {
  bool connect_callback_called = false;
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg");

  fuchsia::virtualization::GuestPtr guest;
  manager.ConnectToGuest(guest.NewRequest(), [&connect_callback_called](auto res) {
    ASSERT_TRUE(res.is_err());
    ASSERT_EQ(ZX_ERR_UNAVAILABLE, res.err());
    connect_callback_called = true;
  });
  ASSERT_TRUE(connect_callback_called);
  guest.Unbind();

  bool launch_callback_called = false;
  fuchsia::virtualization::GuestConfig user_guest_config;
  manager.LaunchGuest(std::move(user_guest_config), guest.NewRequest(),
                      [&launch_callback_called](auto res) {
                        ASSERT_FALSE(res.is_err());
                        launch_callback_called = true;
                      });
  ASSERT_TRUE(launch_callback_called);
  guest.Unbind();

  connect_callback_called = false;
  manager.ConnectToGuest(guest.NewRequest(), [&connect_callback_called](auto res) {
    ASSERT_FALSE(res.is_err());
    connect_callback_called = true;
  });
  ASSERT_TRUE(connect_callback_called);
}

TEST_F(GuestManagerTest, DuplicateListenersProvidedByUserGuestConfig) {
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg");
  fuchsia::virtualization::GuestConfig user_guest_config;

  // Two listeners with the same port.
  const uint32_t host_port = 12345;
  user_guest_config.mutable_vsock_listeners()->push_back(
      {host_port, fidl::InterfaceHandle<fuchsia::virtualization::HostVsockAcceptor>()});
  user_guest_config.mutable_vsock_listeners()->push_back(
      {host_port, fidl::InterfaceHandle<fuchsia::virtualization::HostVsockAcceptor>()});

  fuchsia::virtualization::GuestPtr guest;
  bool launch_callback_called = false;
  fuchsia::virtualization::GuestManager_LaunchGuest_Result result;
  manager.LaunchGuest(std::move(user_guest_config), guest.NewRequest(),
                      [&result, &launch_callback_called](auto res) {
                        result = std::move(res);
                        launch_callback_called = true;
                      });

  ASSERT_TRUE(launch_callback_called);
  ASSERT_EQ(result.err(), ZX_ERR_INVALID_ARGS);
}

TEST_F(GuestManagerTest, UserProvidedInitialListeners) {
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg");
  fuchsia::virtualization::GuestConfig user_guest_config;

  fidl::InterfaceHandle<fuchsia::virtualization::HostVsockAcceptor> acceptor1, acceptor2;

  // Give the handles valid channels (although the endpoint will go unused).
  auto request1 = acceptor1.NewRequest();
  auto request2 = acceptor2.NewRequest();

  user_guest_config.mutable_vsock_listeners()->push_back({123, std::move(acceptor1)});
  user_guest_config.mutable_vsock_listeners()->push_back({456, std::move(acceptor2)});

  fuchsia::virtualization::GuestPtr guest;
  bool launch_callback_called = false;
  fuchsia::virtualization::GuestManager_LaunchGuest_Result result;
  manager.LaunchGuest(std::move(user_guest_config), guest.NewRequest(),
                      [&launch_callback_called](auto res) {
                        ASSERT_TRUE(res.is_response());
                        launch_callback_called = true;
                      });
  ASSERT_TRUE(launch_callback_called);

  bool get_callback_called = false;
  manager.Get([&get_callback_called](fuchsia::virtualization::GuestConfig config) {
    get_callback_called = true;

    // Initial Listeners are passed to the VMM via the guest config.
    ASSERT_TRUE(config.has_vsock_listeners());
    ASSERT_EQ(config.vsock_listeners().size(), 2ul);
  });
  ASSERT_TRUE(get_callback_called);
}

}  // namespace
