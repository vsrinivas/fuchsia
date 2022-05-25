// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_manager/guest_manager.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <test/placeholders/cpp/fidl.h>

#include "fuchsia/virtualization/cpp/fidl.h"

namespace {

class GuestManagerTest : public gtest::TestLoopFixture {
 public:
  sys::testing::ComponentContextProvider provider_;
};

TEST_F(GuestManagerTest, LaunchFailInvalidPath) {
  GuestManager manager(provider_.context(), "/pkg/", "invalid_path.cfg");
  bool launch_callback_called = false;
  manager.LaunchGuest({}, {}, [&launch_callback_called](auto res) {
    ASSERT_TRUE(res.is_err());
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, res.err());
    launch_callback_called = true;
  });
  ASSERT_TRUE(launch_callback_called);
}

TEST_F(GuestManagerTest, LaunchFailInvalidConfig) {
  GuestManager manager(provider_.context(), "/pkg/", "data/configs/bad_schema_invalid_field.cfg");
  bool launch_callback_called = false;
  manager.LaunchGuest({}, {}, [&launch_callback_called](auto res) {
    ASSERT_TRUE(res.is_err());
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, res.err());
    launch_callback_called = true;
  });
  ASSERT_TRUE(launch_callback_called);
}

TEST_F(GuestManagerTest, LaunchAndApplyUserGuestConfig) {
  GuestManager manager(provider_.context(), "/pkg/", "data/configs/valid_guest.cfg");
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
  GuestManager manager(provider_.context(), "/pkg/", "data/configs/valid_guest.cfg");
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
  GuestManager manager(provider_.context(), "/pkg/", "data/configs/valid_guest.cfg");

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

TEST_F(GuestManagerTest, ConnectToGuest) {
  bool connect_callback_called = false;
  GuestManager manager(provider_.context(), "/pkg/", "data/configs/valid_guest.cfg");

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
}  // namespace
