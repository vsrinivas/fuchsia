// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>

#include "src/virtualization/bin/vmm/device/tests/test_with_device.h"
#include "src/virtualization/bin/vmm/device/tests/virtio_queue_fake.h"

namespace {

constexpr uint16_t kNumQueues = 2;
constexpr uint16_t kQueueSize = 16;

constexpr auto kComponentUrl = "#meta/virtio_console.cm";
constexpr auto kComponentName = "virtio_console";

class VirtioConsoleTest : public TestWithDevice {
 protected:
  VirtioConsoleTest()
      : rx_queue_(phys_mem_, PAGE_SIZE * kNumQueues, kQueueSize),
        tx_queue_(phys_mem_, rx_queue_.end(), kQueueSize) {}

  void SetUp() override {
    using component_testing::ChildRef;
    using component_testing::ParentRef;
    using component_testing::Protocol;
    using component_testing::RealmBuilder;
    using component_testing::RealmRoot;
    using component_testing::Route;

    auto realm_builder = RealmBuilder::Create();
    realm_builder.AddChild(kComponentName, kComponentUrl);

    realm_builder
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::logger::LogSink::Name_},
                                Protocol{fuchsia::tracing::provider::Registry::Name_},
                            },
                        .source = ParentRef(),
                        .targets = {ChildRef{kComponentName}}})
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::virtualization::hardware::VirtioConsole::Name_},
                            },
                        .source = ChildRef{kComponentName},
                        .targets = {ParentRef()}});

    realm_ = std::make_unique<RealmRoot>(realm_builder.Build(dispatcher()));
    console_ = realm_->ConnectSync<fuchsia::virtualization::hardware::VirtioConsole>();

    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = MakeStartInfo(tx_queue_.end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

    // Setup console socket.
    status = zx::socket::create(ZX_SOCKET_STREAM, &socket_, &remote_socket_);
    ASSERT_EQ(ZX_OK, status);

    status = console_->Start(std::move(start_info), std::move(remote_socket_));
    ASSERT_EQ(ZX_OK, status);

    // Configure device queues.
    VirtioQueueFake* queues[kNumQueues] = {&rx_queue_, &tx_queue_};
    for (uint16_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(PAGE_SIZE * i, PAGE_SIZE);
      status = console_->ConfigureQueue(i, q->size(), q->desc(), q->avail(), q->used());
      ASSERT_EQ(ZX_OK, status);
    }

    // Finish negotiating features.
    status = console_->Ready(0);
    ASSERT_EQ(ZX_OK, status);
  }

  // Note: use of sync can be problematic here if the test environment needs to handle
  // some incoming FIDL requests.
  fuchsia::virtualization::hardware::VirtioConsoleSyncPtr console_;
  VirtioQueueFake rx_queue_;
  VirtioQueueFake tx_queue_;
  zx::socket socket_;
  zx::socket remote_socket_;
  std::unique_ptr<component_testing::RealmRoot> realm_;
};

TEST_F(VirtioConsoleTest, Receive) {
  void* data_1;
  void* data_2;
  zx_status_t status = DescriptorChainBuilder(rx_queue_)
                           .AppendWritableDescriptor(&data_1, 6)
                           .AppendWritableDescriptor(&data_2, 6)
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  char input[] = "hello\0world";
  size_t actual;
  status = socket_.write(0, input, sizeof(input), &actual);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(sizeof(input), actual);

  status = console_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_STREQ("hello", static_cast<char*>(data_1));
  EXPECT_STREQ("world", static_cast<char*>(data_2));
}

TEST_F(VirtioConsoleTest, Transmit) {
  zx_status_t status = DescriptorChainBuilder(tx_queue_)
                           .AppendReadableDescriptor("hello ", sizeof("hello ") - 1)
                           .AppendReadableDescriptor("world", sizeof("world"))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = console_->NotifyQueue(1);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  char buf[16] = {};
  size_t actual;
  status = socket_.read(0, buf, sizeof(buf), &actual);
  ASSERT_EQ(ZX_OK, status);
  char output[] = "hello world";
  EXPECT_EQ(sizeof(output), actual);
  EXPECT_STREQ(output, buf);
}

}  // namespace
