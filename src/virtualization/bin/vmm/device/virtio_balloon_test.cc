// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <threads.h>

#include <virtio/balloon.h>

#include "fuchsia/logger/cpp/fidl.h"
#include "fuchsia/virtualization/hardware/cpp/fidl.h"
#include "src/virtualization/bin/vmm/device/test_with_device.h"
#include "src/virtualization/bin/vmm/device/virtio_queue_fake.h"

static constexpr uint16_t kNumQueues = 3;
static constexpr uint16_t kQueueSize = 16;

constexpr auto kCppComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_balloon#meta/virtio_balloon.cm";
constexpr auto kRustComponentUrl =
    "fuchsia-pkg://fuchsia.com/virtio_balloon_rs#meta/virtio_balloon_rs.cm";

struct VirtioBalloonTestParam {
  std::string test_name;
  std::string component_url;
};

class VirtioBalloonTest : public TestWithDevice,
                          public ::testing::WithParamInterface<VirtioBalloonTestParam> {
 protected:
  VirtioBalloonTest()
      : inflate_queue_(phys_mem_, PAGE_SIZE * kNumQueues, kQueueSize),
        deflate_queue_(phys_mem_, inflate_queue_.end(), kQueueSize),
        stats_queue_(phys_mem_, deflate_queue_.end(), 1) {}

  bool IsRustComponent() { return GetParam().component_url == kRustComponentUrl; }

  void SetUp() override {
    using component_testing::ChildRef;
    using component_testing::ParentRef;
    using component_testing::Protocol;
    using component_testing::RealmBuilder;
    using component_testing::RealmRoot;
    using component_testing::Route;

    constexpr auto kComponentName = "virtio_balloon";
    // Add extra memory pages which we will be zero'ing inside of the inflate test
    // Not having extra memory will result in inflate test zero op stomping on its own inflate
    // queue while queue is being processed
    constexpr auto kNumExtraTestMemoryPages = 10;

    auto realm_builder = RealmBuilder::Create();
    realm_builder.AddChild(kComponentName, GetParam().component_url);

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
                                Protocol{fuchsia::virtualization::hardware::VirtioBalloon::Name_},
                            },
                        .source = ChildRef{kComponentName},
                        .targets = {ParentRef()}});

    realm_ = std::make_unique<RealmRoot>(realm_builder.Build(dispatcher()));
    balloon_ = realm_->ConnectSync<fuchsia::virtualization::hardware::VirtioBalloon>();

    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status =
        MakeStartInfo(stats_queue_.end() + kNumExtraTestMemoryPages * PAGE_SIZE, &start_info);
    ASSERT_EQ(ZX_OK, status);

    status = balloon_->Start(std::move(start_info));
    ASSERT_EQ(ZX_OK, status);

    // Configure device queues.
    VirtioQueueFake* queues[kNumQueues] = {&inflate_queue_, &deflate_queue_, &stats_queue_};
    for (uint16_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(PAGE_SIZE * i, PAGE_SIZE);
      status = balloon_->ConfigureQueue(i, q->size(), q->desc(), q->avail(), q->used());
      ASSERT_EQ(ZX_OK, status);
    }

    status = balloon_->Ready(VIRTIO_BALLOON_F_STATS_VQ);
    ASSERT_EQ(ZX_OK, status);
  }

 public:
  // Note: use of sync can be problematic here if the test environment needs to handle
  // some incoming FIDL requests.
  fuchsia::virtualization::hardware::VirtioBalloonSyncPtr balloon_;
  VirtioQueueFake inflate_queue_;
  VirtioQueueFake deflate_queue_;
  VirtioQueueFake stats_queue_;
  using TestWithDevice::WaitOnInterrupt;
  std::unique_ptr<component_testing::RealmRoot> realm_;
};

// Do not inflate pages which contain device queue to avoid zeroing out queue while it's being
// processed
TEST_P(VirtioBalloonTest, Inflate) {
  uint32_t pfns[] = {5, 6, 7, 22, 9};
  zx_status_t status =
      DescriptorChainBuilder(inflate_queue_).AppendReadableDescriptor(pfns, sizeof(pfns)).Build();
  ASSERT_EQ(ZX_OK, status);

  status = balloon_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  uint32_t pfns2[] = {8, 10, 9};
  status =
      DescriptorChainBuilder(inflate_queue_).AppendReadableDescriptor(pfns2, sizeof(pfns2)).Build();
  ASSERT_EQ(ZX_OK, status);

  status = balloon_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);
}

TEST_P(VirtioBalloonTest, Deflate) {
  uint32_t pfns[] = {3, 2, 1};
  zx_status_t status =
      DescriptorChainBuilder(deflate_queue_).AppendReadableDescriptor(pfns, sizeof(pfns)).Build();
  ASSERT_EQ(ZX_OK, status);

  status = balloon_->NotifyQueue(1);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);
}

TEST_P(VirtioBalloonTest, Stats) {
  // TODO(fxbug.dev/100513): Enable this test for the rust device.
  if (IsRustComponent()) {
    GTEST_SKIP();
  }
  zx_status_t status =
      DescriptorChainBuilder(stats_queue_).AppendReadableDescriptor(nullptr, 0).Build();
  ASSERT_EQ(ZX_OK, status);

  auto entry = [](void* arg) {
    auto test = static_cast<VirtioBalloonTest*>(arg);
    zx_status_t status = test->WaitOnInterrupt();
    if (status != ZX_OK) {
      return status;
    }
    virtio_balloon_stat_t stat = {.tag = 2301, .val = 1985};
    status = DescriptorChainBuilder(test->stats_queue_)
                 .AppendReadableDescriptor(&stat, sizeof(stat))
                 .Build();
    if (status != ZX_OK) {
      return status;
    }
    return test->balloon_->NotifyQueue(2);
  };
  thrd_t thread;
  int ret = thrd_create_with_name(&thread, entry, this, "balloon-stats");
  ASSERT_EQ(thrd_success, ret);

  zx_status_t stats_status;
  fidl::VectorPtr<fuchsia::virtualization::MemStat> mem_stats;
  status = balloon_->GetMemStats(&stats_status, &mem_stats);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(ZX_OK, stats_status);

  ASSERT_EQ(1u, mem_stats->size());
  EXPECT_EQ(2301u, (*mem_stats)[0].tag);
  EXPECT_EQ(1985u, (*mem_stats)[0].val);

  ret = thrd_join(thread, &status);
  ASSERT_EQ(thrd_success, ret);
  ASSERT_EQ(ZX_OK, status);
}

TEST_P(VirtioBalloonTest, StatsShouldWait) {
  // TODO(fxbug.dev/100513): Enable this test for the rust device.
  if (IsRustComponent()) {
    GTEST_SKIP();
  }
  zx_status_t stats_status;
  fidl::VectorPtr<fuchsia::virtualization::MemStat> mem_stats;
  zx_status_t status = balloon_->GetMemStats(&stats_status, &mem_stats);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(ZX_ERR_SHOULD_WAIT, stats_status);
}

INSTANTIATE_TEST_SUITE_P(VirtioBalloonComponentsTest, VirtioBalloonTest,
                         testing::Values(VirtioBalloonTestParam{"cpp", kCppComponentUrl},
                                         VirtioBalloonTestParam{"rust", kRustComponentUrl}),
                         [](const testing::TestParamInfo<VirtioBalloonTestParam>& info) {
                           return info.param.test_name;
                         });
