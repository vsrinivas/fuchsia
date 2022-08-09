// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_manager/guest_manager.h"

#include <lib/sys/cpp/testing/component_context_provider.h>

#include <test/placeholders/cpp/fidl.h>

#include "fuchsia/virtualization/cpp/fidl.h"
#include "fuchsia/virtualization/cpp/fidl_test_base.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace {

class FakeGuestVsockEndpoint
    : public fuchsia::virtualization::testing::GuestVsockEndpoint_TestBase {
 public:
  void SetContextId(
      uint32_t cid,
      ::fidl::InterfaceHandle<::fuchsia::virtualization::HostVsockConnector> connector,
      ::fidl::InterfaceRequest<::fuchsia::virtualization::GuestVsockAcceptor> acceptor) override {
    cid_ = cid;
  }

  void NotImplemented_(const std::string& name) override {
    ASSERT_TRUE(false) << "Method not supported by FakeManager: " << name;
  }

  fidl::InterfaceRequestHandler<fuchsia::virtualization::GuestVsockEndpoint> GetHandler() {
    return bindings_.GetHandler(this);
  }

  uint32_t cid_ = 0;
  fidl::BindingSet<fuchsia::virtualization::GuestVsockEndpoint> bindings_;
};

class GuestManagerTest : public gtest::TestLoopFixture, public ::testing::WithParamInterface<bool> {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    provider_.service_directory_provider()->AddService(fake_guest_vsock_endpoint_.GetHandler());
  }

  bool UseLegacyVsockDevice() const { return GetParam(); }

  sys::testing::ComponentContextProvider provider_;
  FakeGuestVsockEndpoint fake_guest_vsock_endpoint_;
};

INSTANTIATE_TEST_SUITE_P(UseLegacyVsockDevice, GuestManagerTest, ::testing::Bool(),
                         [](const testing::TestParamInfo<bool>& info) {
                           return info.param ? "LegacyVsockDevice" : "OutOfProcessVsockDevice";
                         });

TEST_P(GuestManagerTest, LaunchFailInvalidPath) {
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "invalid_path.cfg",
                       UseLegacyVsockDevice());
  bool launch_callback_called = false;
  manager.LaunchGuest({}, {}, [&launch_callback_called](auto res) {
    ASSERT_TRUE(res.is_err());
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, res.err());
    launch_callback_called = true;
  });
  ASSERT_TRUE(launch_callback_called);
}

TEST_P(GuestManagerTest, LaunchFailInvalidConfig) {
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/",
                       "data/configs/bad_schema_invalid_field.cfg", UseLegacyVsockDevice());
  bool launch_callback_called = false;
  manager.LaunchGuest({}, {}, [&launch_callback_called](auto res) {
    ASSERT_TRUE(res.is_err());
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, res.err());
    launch_callback_called = true;
  });
  ASSERT_TRUE(launch_callback_called);
}

TEST_P(GuestManagerTest, LaunchAndApplyUserGuestConfig) {
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg",
                       UseLegacyVsockDevice());
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

TEST_P(GuestManagerTest, DoubleLaunchFail) {
  bool launch_callback_called = false;
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg",
                       UseLegacyVsockDevice());
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

TEST_P(GuestManagerTest, LaunchAndGetInfo) {
  bool get_callback_called = false;
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg",
                       UseLegacyVsockDevice());

  manager.GetGuestInfo([&get_callback_called](auto info) {
    ASSERT_EQ(info.guest_status, fuchsia::virtualization::GuestStatus::NOT_STARTED);
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

  get_callback_called = false;
  manager.GetGuestInfo([&get_callback_called](auto info) {
    ASSERT_EQ(info.guest_status, fuchsia::virtualization::GuestStatus::STARTED);
    get_callback_called = true;
  });
  ASSERT_TRUE(get_callback_called);
}

TEST_P(GuestManagerTest, ConnectToGuest) {
  bool connect_callback_called = false;
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg",
                       UseLegacyVsockDevice());

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

TEST_P(GuestManagerTest, LaunchAndUseVsock) {
  if (!UseLegacyVsockDevice()) {
    // Guest manager doesn't create a host vsock when not using the legacy device.
    GTEST_SKIP();
  }

  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg",
                       /*use_legacy_vsock_device=*/true);
  fuchsia::virtualization::GuestConfig user_guest_config;
  user_guest_config.mutable_cmdline_add()->emplace_back("extra_cmd_line_arg=0");

  fuchsia::virtualization::GuestPtr guest;
  bool launch_callback_called = false;
  ASSERT_EQ(fake_guest_vsock_endpoint_.cid_, 0u);
  manager.LaunchGuest(std::move(user_guest_config), guest.NewRequest(), [&](auto res) {
    ASSERT_FALSE(res.is_err());
    launch_callback_called = true;
  });
  ASSERT_TRUE(launch_callback_called);
  fuchsia::virtualization::HostVsockEndpoint_Connect2_Result res;

  RunLoopUntilIdle();
  ASSERT_EQ(fake_guest_vsock_endpoint_.cid_, fuchsia::virtualization::DEFAULT_GUEST_CID);

  bool get_callback_called = false;
  manager.Get([&get_callback_called](fuchsia::virtualization::GuestConfig config) {
    ASSERT_EQ("test cmdline extra_cmd_line_arg=0", config.cmdline());

    get_callback_called = true;
  });
  ASSERT_TRUE(get_callback_called);
}

TEST_P(GuestManagerTest, DuplicateListenersProvidedByUserGuestConfig) {
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg",
                       UseLegacyVsockDevice());
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
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg",
                       /*use_legacy_vsock_device=*/false);
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

TEST_F(GuestManagerTest, UserProvidedInitialListenersLegacyDevice) {
  GuestManager manager(dispatcher(), provider_.context(), "/pkg/", "data/configs/valid_guest.cfg",
                       /*use_legacy_vsock_device=*/true);
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

    // Listener list should be empty when using the legacy vsock device.
    ASSERT_TRUE(config.has_vsock_listeners());
    ASSERT_TRUE(config.vsock_listeners().empty());
  });
  ASSERT_TRUE(get_callback_called);

  fidl::InterfaceHandle<fuchsia::virtualization::HostVsockEndpoint> endpoint;
  manager.GetHostVsockEndpoint(endpoint.NewRequest());

  auto ptr = endpoint.Bind();
  bool listen_callback_called1 = false, listen_callback_called2 = false;
  fidl::InterfaceHandle<fuchsia::virtualization::HostVsockAcceptor> acceptor3, acceptor4;

  // Unused server ends to make the channel valid.
  auto request3 = acceptor3.NewRequest();
  auto request4 = acceptor4.NewRequest();

  // A listener passed via config should already be bound to this port, so the Listen should fail.
  ptr->Listen(
      123, std::move(acceptor3),
      [&listen_callback_called1](fuchsia::virtualization::HostVsockEndpoint_Listen_Result result) {
        ASSERT_TRUE(result.is_err());
        listen_callback_called1 = true;
      });

  RunLoopUntilIdle();
  ASSERT_TRUE(listen_callback_called1);

  // This port is unused, and so should be Listen-able.
  ptr->Listen(
      789, std::move(acceptor4),
      [&listen_callback_called2](fuchsia::virtualization::HostVsockEndpoint_Listen_Result result) {
        ASSERT_TRUE(result.is_response());
        listen_callback_called2 = true;
      });

  RunLoopUntilIdle();
  ASSERT_TRUE(listen_callback_called2);
}

}  // namespace
