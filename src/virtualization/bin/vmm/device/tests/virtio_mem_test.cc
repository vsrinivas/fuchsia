// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <threads.h>

#include "src/virtualization/bin/vmm/device/tests/test_with_device.h"
#include "src/virtualization/bin/vmm/device/tests/virtio_queue_fake.h"

static constexpr uint16_t kNumQueues = 1;
static constexpr uint16_t kQueueSize = 16;

static constexpr uint64_t kPluggedBlockSize = 4 * 1024 * 1024;
static constexpr uint64_t kPluggedRegionSize = static_cast<uint64_t>(8) * 1024 * 1024 * 1024;

class VirtioMemTest : public TestWithDevice {
 public:
  constexpr static auto kComponentName = "virtio_mem";

 protected:
  VirtioMemTest() : guest_request_queue_(phys_mem_, PAGE_SIZE * kNumQueues, kQueueSize) {}

  void SetUp() override {
    using component_testing::ChildRef;
    using component_testing::ParentRef;
    using component_testing::Protocol;
    using component_testing::RealmBuilder;
    using component_testing::RealmRoot;
    using component_testing::Route;

    constexpr auto kVirtioMemUrl = "#meta/virtio_mem.cm";
    // Add extra memory pages which we will be zero'ing inside of the test
    constexpr auto kNumExtraTestMemoryPages = 1024;

    auto realm_builder = RealmBuilder::Create();
    realm_builder.AddChild(kComponentName, kVirtioMemUrl);

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
                                Protocol{fuchsia::virtualization::hardware::VirtioMem::Name_},
                            },
                        .source = ChildRef{kComponentName},
                        .targets = {ParentRef()}});

    realm_ = std::make_unique<RealmRoot>(realm_builder.Build(dispatcher()));
    mem_ = realm_->ConnectSync<fuchsia::virtualization::hardware::VirtioMem>();

    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = MakeStartInfo(
        guest_request_queue_.end() + kNumExtraTestMemoryPages * PAGE_SIZE, &start_info);
    ASSERT_EQ(ZX_OK, status);

    status = mem_->Start(std::move(start_info), kPluggedBlockSize, kPluggedRegionSize);
    ASSERT_EQ(ZX_OK, status);

    // Configure device queues.
    guest_request_queue_.Configure(0, PAGE_SIZE);
    status = mem_->ConfigureQueue(0, guest_request_queue_.size(), guest_request_queue_.desc(),
                                  guest_request_queue_.avail(), guest_request_queue_.used());
    ASSERT_EQ(ZX_OK, status);

    status = mem_->Ready(0);
    ASSERT_EQ(ZX_OK, status);
  }

  template <typename T>
  T InspectValue(std::string value_name) {
    return GetInspect("realm_builder\\:" + realm_->GetChildName() + "/" + kComponentName + ":root",
                      kComponentName)
        .GetByPath({"root", kComponentName, std::move(value_name)})
        .Get<T>();
  }

 public:
  // Note: use of sync can be problematic here if the test environment needs to handle
  // some incoming FIDL requests.
  fuchsia::virtualization::hardware::VirtioMemSyncPtr mem_;
  VirtioQueueFake guest_request_queue_;
  using TestWithDevice::WaitOnInterrupt;
  std::unique_ptr<component_testing::RealmRoot> realm_;
};

TEST_F(VirtioMemTest, Placeholder) {}
