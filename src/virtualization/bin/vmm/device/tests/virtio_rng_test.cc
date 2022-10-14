// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>

#include "src/virtualization/bin/vmm/device/tests/test_with_device.h"
#include "src/virtualization/bin/vmm/device/tests/virtio_queue_fake.h"

static constexpr uint16_t kQueueSize = 16;

class VirtioRngTest : public TestWithDevice {
 protected:
  VirtioRngTest() : queue_(phys_mem_, PAGE_SIZE * 1, kQueueSize) {}

  void SetUp() override {
    using component_testing::ChildRef;
    using component_testing::Directory;
    using component_testing::ParentRef;
    using component_testing::Protocol;
    using component_testing::RealmBuilder;
    using component_testing::RealmRoot;
    using component_testing::Route;

    constexpr auto kComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_rng#meta/virtio_rng.cm";
    constexpr auto kComponentName = "virtio_rng";

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
                                Protocol{fuchsia::virtualization::hardware::VirtioRng::Name_},
                            },
                        .source = ChildRef{kComponentName},
                        .targets = {ParentRef()}});

    realm_ = std::make_unique<RealmRoot>(realm_builder.Build(dispatcher()));
    rng_ = realm_->ConnectSync<fuchsia::virtualization::hardware::VirtioRng>();

    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = MakeStartInfo(queue_.end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

    status = rng_->Start(std::move(start_info));
    ASSERT_EQ(ZX_OK, status);

    // Configure device queues.
    queue_.Configure(PAGE_SIZE * 0, PAGE_SIZE);
    status = rng_->ConfigureQueue(0, queue_.size(), queue_.desc(), queue_.avail(), queue_.used());
    ASSERT_EQ(ZX_OK, status);

    // Finish negotiating features.
    status = rng_->Ready(0);
    ASSERT_EQ(ZX_OK, status);
  }

  // Note: use of sync can be problematic here if the test environment needs to handle
  // some incoming FIDL requests.
  fuchsia::virtualization::hardware::VirtioRngSyncPtr rng_;
  VirtioQueueFake queue_;
  std::unique_ptr<component_testing::RealmRoot> realm_;
};

TEST_F(VirtioRngTest, Entropy) {
  const size_t kEntropyLen = 16;
  void *data[8];

  DescriptorChainBuilder builder(queue_);
  for (auto &it : data) {
    builder.AppendWritableDescriptor(&it, kEntropyLen);
  }
  zx_status_t status = builder.Build();
  ASSERT_EQ(ZX_OK, status);

  status = rng_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  // Check if any of our requested entropies are the same by inserting them all
  // in a set and ensuring no duplicates. If our entropy source is truly random,
  // then the probability that we legitimately get duplicate entropy data,
  // and hence a spurious test failure, is 8! / 2^128 ~= 1.1*10^-34.
  std::set<std::vector<char>> entropy_set;
  for (auto it : data) {
    char *raw_entropy = static_cast<char *>(it);
    std::vector<char> entropy(&raw_entropy[0], &raw_entropy[kEntropyLen]);
    bool result = entropy_set.insert(std::move(entropy)).second;
    ASSERT_TRUE(result);
  }
}
