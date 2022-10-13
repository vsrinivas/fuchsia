// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <threads.h>

#include <virtio/balloon.h>

#include "src/virtualization/bin/vmm/device/tests/test_with_device.h"
#include "src/virtualization/bin/vmm/device/tests/virtio_queue_fake.h"

static constexpr uint16_t kNumQueues = 3;
static constexpr uint16_t kQueueSize = 16;

class VirtioBalloonTest : public TestWithDevice {
 protected:
  VirtioBalloonTest()
      : inflate_queue_(phys_mem_, PAGE_SIZE * kNumQueues, kQueueSize),
        deflate_queue_(phys_mem_, inflate_queue_.end(), kQueueSize),
        stats_queue_(phys_mem_, deflate_queue_.end(), 1) {}

  void SetUp() override {
    using component_testing::ChildRef;
    using component_testing::ParentRef;
    using component_testing::Protocol;
    using component_testing::RealmBuilder;
    using component_testing::RealmRoot;
    using component_testing::Route;

    constexpr auto kComponentName = "virtio_balloon";
    constexpr auto kVirtioBalloonUrl =
        "fuchsia-pkg://fuchsia.com/virtio_balloon#meta/virtio_balloon.cm";
    // Add extra memory pages which we will be zero'ing inside of the inflate test
    // Not having extra memory will result in inflate test zero op stomping on its own inflate
    // queue while queue is being processed
    constexpr auto kNumExtraTestMemoryPages = 10;

    auto realm_builder = RealmBuilder::Create();
    realm_builder.AddChild(kComponentName, kVirtioBalloonUrl);

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
TEST_F(VirtioBalloonTest, Inflate) {
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

TEST_F(VirtioBalloonTest, Deflate) {
  uint32_t pfns[] = {3, 2, 1};
  zx_status_t status =
      DescriptorChainBuilder(deflate_queue_).AppendReadableDescriptor(pfns, sizeof(pfns)).Build();
  ASSERT_EQ(ZX_OK, status);

  status = balloon_->NotifyQueue(1);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);
}

TEST_F(VirtioBalloonTest, Stats) {
  zx_status_t status =
      DescriptorChainBuilder(stats_queue_).AppendReadableDescriptor(nullptr, 0).Build();
  ASSERT_EQ(ZX_OK, status);

  struct Arg {
    VirtioBalloonTest* test;
    virtio_balloon_stat_t stat[2] = {{.tag = 2301, .val = 1985}, {.tag = 3412, .val = 41241}};
    virtio_balloon_stat_t stat2[3] = {
        {.tag = 11, .val = 112211}, {.tag = 22, .val = 223322}, {.tag = 33, .val = 334433}};
  };

  Arg arg{.test = this};

  auto entry = [](void* varg) {
    Arg* arg = static_cast<Arg*>(varg);
    zx_status_t status = arg->test->WaitOnInterrupt();
    if (status != ZX_OK) {
      return status;
    }
    status = DescriptorChainBuilder(arg->test->stats_queue_)
                 .AppendReadableDescriptor(&arg->stat, sizeof(arg->stat))
                 .Build();
    if (status != ZX_OK) {
      return status;
    }
    status = arg->test->balloon_->NotifyQueue(2);
    if (status != ZX_OK) {
      return status;
    }

    status = arg->test->WaitOnInterrupt();
    if (status != ZX_OK) {
      return status;
    }

    status = DescriptorChainBuilder(arg->test->stats_queue_)
                 .AppendReadableDescriptor(&arg->stat2, sizeof(arg->stat2))
                 .Build();
    if (status != ZX_OK) {
      return status;
    }
    return arg->test->balloon_->NotifyQueue(2);
  };
  thrd_t thread;
  int ret = thrd_create_with_name(&thread, entry, &arg, "balloon-stats");
  ASSERT_EQ(thrd_success, ret);

  zx_status_t stats_status;
  fidl::VectorPtr<fuchsia::virtualization::MemStat> mem_stats;
  status = balloon_->GetMemStats(&stats_status, &mem_stats);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(ZX_OK, stats_status);
  ASSERT_EQ(2u, mem_stats->size());
  // We have to use EXPECT_TRUE instead of EXPECT_EQ here to avoid hitting unaligned read UB inside
  // of the EXPECT_EQ EXPECT_EQ takes a reference to passed parameter to be able to log error
  // virtio_balloon_stat_t contains tag is 16bit and val is 64bit.
  // Data has to be packed according to the virtio device spec, making val unaligned
  // Taking a reference to unaligned val causes UB
  EXPECT_TRUE(arg.stat[0].tag == (*mem_stats)[0].tag);
  EXPECT_TRUE(arg.stat[0].val == (*mem_stats)[0].val);
  EXPECT_TRUE(arg.stat[1].tag == (*mem_stats)[1].tag);
  EXPECT_TRUE(arg.stat[1].val == (*mem_stats)[1].val);

  status = balloon_->GetMemStats(&stats_status, &mem_stats);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(ZX_OK, stats_status);

  ASSERT_EQ(3u, mem_stats->size());
  EXPECT_TRUE(arg.stat2[0].tag == (*mem_stats)[0].tag);
  EXPECT_TRUE(arg.stat2[0].val == (*mem_stats)[0].val);
  EXPECT_TRUE(arg.stat2[1].tag == (*mem_stats)[1].tag);
  EXPECT_TRUE(arg.stat2[1].val == (*mem_stats)[1].val);
  EXPECT_TRUE(arg.stat2[2].tag == (*mem_stats)[2].tag);
  EXPECT_TRUE(arg.stat2[2].val == (*mem_stats)[2].val);

  ret = thrd_join(thread, &status);
  ASSERT_EQ(thrd_success, ret);
  ASSERT_EQ(ZX_OK, status);
}
