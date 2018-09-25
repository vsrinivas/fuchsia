// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/guest/device/cpp/fidl.h>
#include <lib/component/cpp/testing/test_with_environment.h>
#include <zx/resource.h>

#include "garnet/bin/guest/vmm/device/virtio_queue_fake.h"

static constexpr char kVirtioConsoleUrl[] = "virtio_console";
static constexpr char kRealm[] = "virtio-console-test-realm";
static constexpr uint16_t kQueueSize = 16;

class VirtioConsoleTest : public component::testing::TestWithEnvironment {
 protected:
  VirtioConsoleTest()
      : rx_queue_(phys_mem_, PAGE_SIZE * 2, kQueueSize),
        tx_queue_(phys_mem_, rx_queue_.end(), kQueueSize) {}

  void SetUp() override {
    enclosing_environment_ =
        CreateNewEnclosingEnvironment(kRealm, CreateServices());
    bool started = WaitForEnclosingEnvToStart(enclosing_environment_.get());
    ASSERT_TRUE(started);

    component::Services services;
    fuchsia::sys::LaunchInfo launch_info{
        .url = kVirtioConsoleUrl,
        .directory_request = services.NewRequest(),
    };
    component_controller_ =
        enclosing_environment_->CreateComponent(std::move(launch_info));
    services.ConnectToService(console_.NewRequest());

    zx_status_t status = zx::event::create(0, &event_);
    ASSERT_EQ(ZX_OK, status);

    status = phys_mem_.Init(tx_queue_.end());
    ASSERT_EQ(ZX_OK, status);
    rx_queue_.Configure(PAGE_SIZE * 0, PAGE_SIZE);
    tx_queue_.Configure(PAGE_SIZE * 1, PAGE_SIZE);

    fuchsia::guest::device::StartInfo start_info;
    status = event_.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_SIGNAL,
                              &start_info.event);
    ASSERT_EQ(ZX_OK, status);
    status = phys_mem_.vmo().duplicate(
        ZX_RIGHT_TRANSFER | ZX_RIGHTS_IO | ZX_RIGHT_MAP, &start_info.vmo);
    ASSERT_EQ(ZX_OK, status);
    status = zx::socket::create(ZX_SOCKET_STREAM, &socket_, &remote_socket_);
    ASSERT_EQ(ZX_OK, status);

    status = console_->Start(std::move(start_info), std::move(remote_socket_));
    ASSERT_EQ(ZX_OK, status);
    status = console_->ConfigureQueue(0, rx_queue_.size(), rx_queue_.desc(),
                                      rx_queue_.avail(), rx_queue_.used());
    ASSERT_EQ(ZX_OK, status);
    status = console_->ConfigureQueue(1, tx_queue_.size(), tx_queue_.desc(),
                                      tx_queue_.avail(), tx_queue_.used());
    ASSERT_EQ(ZX_OK, status);
  }

  std::unique_ptr<component::testing::EnclosingEnvironment>
      enclosing_environment_;
  fuchsia::sys::ComponentControllerPtr component_controller_;
  fuchsia::guest::device::VirtioConsoleSyncPtr console_;

  zx::event event_;
  machina::PhysMem phys_mem_;
  VirtioQueueFake rx_queue_;
  VirtioQueueFake tx_queue_;
  zx::socket socket_;
  zx::socket remote_socket_;
};

TEST_F(VirtioConsoleTest, Transmit) {
  zx_status_t status =
      DescriptorChainBuilder(tx_queue_)
          .AppendReadableDescriptor("hello ", sizeof("hello ") - 1)
          .AppendReadableDescriptor("world", sizeof("world"))
          .Build();
  ASSERT_EQ(ZX_OK, status);

  status = console_->NotifyQueue(1);
  ASSERT_EQ(ZX_OK, status);

  zx::time deadline = zx::deadline_after(zx::sec(5));
  zx_signals_t pending;
  status = event_.wait_one(ZX_USER_SIGNAL_ALL, deadline, &pending);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_TRUE(pending & ZX_USER_SIGNAL_ALL);

  char buf[16] = {};
  size_t actual;
  status = socket_.read(0, buf, sizeof(buf), &actual);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(sizeof("hello world"), actual);
  EXPECT_STREQ("hello world", buf);
}
