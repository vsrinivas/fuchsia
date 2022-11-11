// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <threads.h>

#include <cstdint>
#include <memory>
#include <numeric>

#include <virtio/balloon.h>
#include <virtio/virtio_ring.h>

#include "src/virtualization/bin/vmm/device/tests/test_with_device.h"
#include "src/virtualization/bin/vmm/device/tests/virtio_queue_fake.h"

static constexpr uint16_t kNumQueues = 4;
static constexpr uint16_t kQueueSize = 16;
static constexpr size_t kDataSizes[kNumQueues] = {PAGE_SIZE, PAGE_SIZE, PAGE_SIZE,
                                                  PAGE_SIZE * 1024};

zx_gpaddr_t PageAlign(zx_gpaddr_t addr) { return addr + (PAGE_SIZE - addr % PAGE_SIZE); }

class VirtioBalloonTest : public TestWithDevice {
 public:
  constexpr static auto kComponentName = "virtio_balloon";

 protected:
  VirtioBalloonTest()
      : inflate_queue_(phys_mem_, PAGE_SIZE * kNumQueues, kQueueSize),
        deflate_queue_(phys_mem_, inflate_queue_.end(), kQueueSize),
        stats_queue_(phys_mem_, deflate_queue_.end(), 1),
        free_page_reporting_queue_(phys_mem_, stats_queue_.end(), kQueueSize),
        queues_mem_size_((free_page_reporting_queue_.end() - inflate_queue_.desc()) / PAGE_SIZE),
        data_mem_size_(std::accumulate(kDataSizes, kDataSizes + kNumQueues, 0)) {}

  void SetUp() override {
    using component_testing::ChildRef;
    using component_testing::ParentRef;
    using component_testing::Protocol;
    using component_testing::RealmBuilder;
    using component_testing::RealmRoot;
    using component_testing::Route;

    constexpr auto kVirtioBalloonUrl = "#meta/virtio_balloon.cm";

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
        MakeStartInfo(PageAlign(free_page_reporting_queue_.end()) + data_mem_size_, &start_info);
    ASSERT_EQ(ZX_OK, status);

    status = balloon_->Start(std::move(start_info));
    ASSERT_EQ(ZX_OK, status);

    // Configure device queues.
    VirtioQueueFake* queues[kNumQueues] = {&inflate_queue_, &deflate_queue_, &stats_queue_,
                                           &free_page_reporting_queue_};
    for (uint16_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(PageAlign(free_page_reporting_queue_.end()) +
                       std::accumulate(kDataSizes, kDataSizes + i, 0),
                   kDataSizes[i]);
      status = balloon_->ConfigureQueue(i, q->size(), q->desc(), q->avail(), q->used());
      ASSERT_EQ(ZX_OK, status);
    }

    status = balloon_->Ready(VIRTIO_BALLOON_F_STATS_VQ | VIRTIO_BALLOON_F_PAGE_POISON |
                             VIRTIO_BALLOON_F_PAGE_REPORTING | (1 << VIRTIO_RING_F_INDIRECT_DESC));
    ASSERT_EQ(ZX_OK, status);
  }

  template <typename T>
  T InspectValue(std::string value_name) {
    return GetInspect("realm_builder\\:" + realm_->GetChildName() + "/" + kComponentName + ":root",
                      kComponentName)
        .GetByPath({"root", std::move(value_name)})
        .Get<T>();
  }

  void ValidateInflatePFNs(uint32_t* begin, uint32_t* end) {
    // Driver memory layout is multiple device queues followed by a data block
    // which is shared by all queues. We don't want to inflate ( zero ) pages
    // which contain device queues because it means inflate might stomp on its
    // own queue.
    for (uint32_t* pfn = begin; pfn < end; pfn++) {
      ASSERT_GT(*pfn, queues_mem_size_ / PAGE_SIZE);
      ASSERT_LT(*pfn, (queues_mem_size_ + data_mem_size_) / PAGE_SIZE);
    }
  }

  constexpr static uint16_t INFLATEQ = 0;
  constexpr static uint16_t DEFLATEQ = 1;
  constexpr static uint16_t STATSQ = 2;
  // See src/virtualization/bin/vmm/device/virtio_balloon/src/wire.rs comment for
  // the REPORTINGVQ to understand why we are not using virtio spec queue index
  // here
  constexpr static uint16_t REPORTINGVQ = 3;

 public:
  // Note: use of sync can be problematic here if the test environment needs to handle
  // some incoming FIDL requests.
  fuchsia::virtualization::hardware::VirtioBalloonSyncPtr balloon_;
  VirtioQueueFake inflate_queue_;
  VirtioQueueFake deflate_queue_;
  VirtioQueueFake stats_queue_;
  VirtioQueueFake free_page_reporting_queue_;
  using TestWithDevice::WaitOnInterrupt;
  std::unique_ptr<component_testing::RealmRoot> realm_;
  size_t queues_mem_size_;
  size_t data_mem_size_;
};

TEST_F(VirtioBalloonTest, Inflate) {
  ASSERT_EQ(InspectValue<int64_t>("num_inflated_pages"), 0);
  // 22 is out of bounds, processing will get up to it and drop the rest of descriptor chain
  uint32_t pfns[] = {15, 16, 17, 22, 19};
  ValidateInflatePFNs(pfns, pfns + sizeof(pfns) / sizeof(*pfns));

  zx_status_t status =
      DescriptorChainBuilder(inflate_queue_).AppendReadableDescriptor(pfns, sizeof(pfns)).Build();
  ASSERT_EQ(ZX_OK, status);

  status = balloon_->NotifyQueue(INFLATEQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  ASSERT_EQ(InspectValue<int64_t>("num_inflated_pages"), 5);

  uint32_t pfns2[] = {8, 10, 9};
  ValidateInflatePFNs(pfns2, pfns2 + sizeof(pfns2) / sizeof(*pfns2));
  status =
      DescriptorChainBuilder(inflate_queue_).AppendReadableDescriptor(pfns2, sizeof(pfns2)).Build();
  ASSERT_EQ(ZX_OK, status);

  status = balloon_->NotifyQueue(INFLATEQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(InspectValue<int64_t>("num_inflated_pages"), 8);
}

TEST_F(VirtioBalloonTest, Deflate) {
  uint32_t pfns[] = {3, 2, 1, 6};
  zx_status_t status =
      DescriptorChainBuilder(deflate_queue_).AppendReadableDescriptor(pfns, sizeof(pfns)).Build();
  ASSERT_EQ(ZX_OK, status);

  status = balloon_->NotifyQueue(DEFLATEQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(InspectValue<int64_t>("num_inflated_pages"), -4);
}

bool IsPtrInRange(uint8_t* ptr, uint8_t* begin, uint8_t* end) { return ptr >= begin && ptr < end; }

TEST_F(VirtioBalloonTest, FreePageReporting_DirectDesc) {
  uint8_t* free_page_ptr = nullptr;
  ASSERT_EQ(InspectValue<uint64_t>("num_reported_free_pages"), 0u);
  uint64_t vmo_size;
  phys_mem_.vmo().get_size(&vmo_size);
  phys_mem_.vmo().op_range(ZX_VMO_OP_COMMIT, 0, vmo_size, nullptr, 0);

  // Use 2MiB which is the minimal size free page report I've seen on Linux in a
  // direct free page report descriptor
  const size_t free_page_len = PAGE_SIZE * 512;
  const auto [data_begin, data_end] = free_page_reporting_queue_.data();
  const size_t data_len = data_end - data_begin;
  uint8_t* data_ptr = reinterpret_cast<uint8_t*>(phys_mem_.ptr(data_begin, data_len));
  std::fill(data_ptr, data_ptr + data_len, 1);

  ASSERT_EQ(DescriptorChainBuilder(free_page_reporting_queue_)
                .AppendWritableDescriptor(&free_page_ptr, free_page_len)
                .Build(),
            ZX_OK);

  ASSERT_EQ(reinterpret_cast<uint64_t>(free_page_ptr) % PAGE_SIZE, 0u);
  zx_status_t status = balloon_->NotifyQueue(REPORTINGVQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  zx_info_vmo_t vmo_info = {};
  ASSERT_EQ(ZX_OK,
            phys_mem_.vmo().get_info(ZX_INFO_VMO, &vmo_info, sizeof(vmo_info), nullptr, nullptr));

  ASSERT_LE(vmo_info.committed_bytes, vmo_size - free_page_len);

  for (size_t i = 0; i < data_len; i++) {
    if (IsPtrInRange(data_ptr + i, free_page_ptr, free_page_ptr + free_page_len)) {
      ASSERT_EQ(0, data_ptr[i]);
    } else {
      ASSERT_EQ(1, data_ptr[i]);
    }
  }
  ASSERT_EQ(InspectValue<uint64_t>("num_reported_free_pages"), free_page_len / PAGE_SIZE);
}

// Free page reporting tests will commit the entire VMO and later check that
// number of commited pages is less or equal to the vmo size minus reported free
// pages. We have to use less or equal comparison in those tests because kernel
// might decide to decommit part of the VMO while testing is being setup.
TEST_F(VirtioBalloonTest, FreePageReporting_MixOfDirectAndIndirectDesc) {
  ASSERT_EQ(InspectValue<uint64_t>("num_reported_free_pages"), 0u);
  uint64_t vmo_size;
  phys_mem_.vmo().get_size(&vmo_size);
  phys_mem_.vmo().op_range(ZX_VMO_OP_COMMIT, 0, vmo_size, nullptr, 0);
  // Allocate 2 indirect memory blocks which we'll refer to in our indirect descriptors
  constexpr auto NUM_INDIRECT_DESCRIPTORS = 2;
  // Use 1 MiB and 2 MiB free page reports which is similar to what you normally get on Linux
  const size_t free_page_len[NUM_INDIRECT_DESCRIPTORS] = {PAGE_SIZE * 256, PAGE_SIZE * 512};
  uint64_t free_pages[NUM_INDIRECT_DESCRIPTORS] = {
      reinterpret_cast<uint64_t>(free_page_reporting_queue_.AllocData(free_page_len[0]).driver_mem),
      reinterpret_cast<uint64_t>(
          free_page_reporting_queue_.AllocData(free_page_len[1]).driver_mem)};

  // Manually create an indirect descriptor chain
  // Use page aligned allocation to be able to compare committed memory before and after the free
  // page report.
  // Without doing page aligned allocation our DirectDescriptor data block will span
  // across memory page boundary making committed memory comparison off by one page
  //
  // use +1 here to add a broken descriptor in the middle and validate indirect chain walking logic
  const size_t indirect_chain_length = sizeof(vring_desc) * (NUM_INDIRECT_DESCRIPTORS + 1);
  vring_desc* indirect_chain = reinterpret_cast<vring_desc*>(
      free_page_reporting_queue_.AllocData(PageAlign(indirect_chain_length)).device_mem);
  // first descriptor in the indirect chain
  indirect_chain[0].addr = free_pages[0];
  ASSERT_EQ(indirect_chain[0].addr % PAGE_SIZE, 0u);
  indirect_chain[0].len = free_page_len[0];
  indirect_chain[0].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
  indirect_chain[0].next = 2;
  // broken descriptor which parsing logic is expected to skip
  indirect_chain[1].addr = 0;
  indirect_chain[1].len = PAGE_SIZE;
  indirect_chain[1].flags = VRING_DESC_F_WRITE;
  indirect_chain[1].next = 0;
  // another normal descriptor
  // walking is expected to get there after descriptor 0
  indirect_chain[2].addr = free_pages[1];
  ASSERT_EQ(indirect_chain[2].addr % PAGE_SIZE, 0u);
  indirect_chain[2].len = free_page_len[1];
  indirect_chain[2].flags = VRING_DESC_F_WRITE;
  indirect_chain[2].next = 0;

  const auto direct_free_page_len = PAGE_SIZE * 128;
  void* direct_free_page_ptr;
  // Linux virtio balloon driver sets VRING_DESC_F_WRITE along with VRING_DESC_F_INDIRECT flag
  // Lets do the same to make sure indirect processing logic follows the spec and ignores the write
  // flag if indirect flag is set
  //
  // 2.7.5.3.2 Device Requirements: Indirect Descriptors
  // The device MUST ignore the write-only flag (flags&VIRTQ_DESC_F_WRITE) in the descriptor that
  // refers to an indirect table.
  ASSERT_EQ(DescriptorChainBuilder(free_page_reporting_queue_)
                .AppendWritableDescriptor(&direct_free_page_ptr, direct_free_page_len)
                .AppendDescriptor(reinterpret_cast<void**>(&indirect_chain), indirect_chain_length,
                                  VRING_DESC_F_INDIRECT | VRING_DESC_F_WRITE)
                .Build(),
            ZX_OK);

  ASSERT_EQ(reinterpret_cast<uint64_t>(direct_free_page_ptr) % PAGE_SIZE, 0u);
  zx_status_t status = balloon_->NotifyQueue(REPORTINGVQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  zx_info_vmo_t vmo_info = {};
  ASSERT_EQ(ZX_OK,
            phys_mem_.vmo().get_info(ZX_INFO_VMO, &vmo_info, sizeof(vmo_info), nullptr, nullptr));
  ASSERT_LE(vmo_info.committed_bytes,
            vmo_size - (free_page_len[0] + free_page_len[1] + direct_free_page_len));
  ASSERT_EQ(InspectValue<uint64_t>("num_reported_free_pages"),
            (free_page_len[0] + free_page_len[1] + direct_free_page_len) / PAGE_SIZE);
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
    status = arg->test->balloon_->NotifyQueue(STATSQ);
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
    return arg->test->balloon_->NotifyQueue(STATSQ);
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
