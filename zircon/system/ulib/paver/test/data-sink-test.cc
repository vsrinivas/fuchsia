// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/paged_vmo.h>
#include <lib/zx/pager.h>
#include <zircon/limits.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "../paver.h"
#include "test-utils.h"

namespace {

constexpr size_t kBlockCount = 10;
constexpr size_t kPageCount = 4;
constexpr uint8_t kData = 0xab;

class MockUserPager {
 public:
  int GetNumPageFaults() const { return num_page_faults_; }

  MockUserPager() : page_request_handler_(this) {
    ASSERT_OK(zx::pager::create(0, &pager_));
    ASSERT_OK(loop_.StartThread("data-sink-test-pager-loop"));
  }

  void CreatePayloadPaged(size_t num_pages, ::llcpp::fuchsia::mem::Buffer* out) {
    zx::vmo vmo;
    size_t vmo_size = num_pages * PAGE_SIZE;

    // Create a vmo backed by |pager_|.
    page_request_handler_.CreateVmo(loop_.dispatcher(), zx::unowned_pager(pager_.get()), 0,
                                    vmo_size, &pager_vmo_);
    // Create and return a resizable COW clone, similar to how system_updater passes in payload
    // vmo's to the paver.
    ASSERT_OK(pager_vmo_.create_child(ZX_VMO_CHILD_COPY_ON_WRITE | ZX_VMO_CHILD_RESIZABLE, 0,
                                      vmo_size, &vmo));
    out->vmo = std::move(vmo);
    out->size = vmo_size;
  }

 private:
  // Dummy page request handler that fulfills page requests from memory.
  void PageRequestHandler(async_dispatcher_t* dispatcher, async::PagedVmoBase* paged_vmo,
                          zx_status_t status, const zx_packet_page_request_t* request) {
    if (request->command != ZX_PAGER_VMO_READ) {
      return;
    }

    // Create a vmo and fill it with a predictable pattern that can be verified later.
    zx::vmo vmo;
    fzl::VmoMapper mapper;
    size_t vmo_size = fbl::round_up(request->length, ZX_PAGE_SIZE);
    ASSERT_OK(mapper.CreateAndMap(vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
    memset(mapper.start(), kData, mapper.size());
    mapper.Unmap();

    // Use the vmo created above to supply pages to the destination vmo.
    ASSERT_OK(pager_.supply_pages(pager_vmo_, request->offset, request->length, vmo, 0));
    num_page_faults_++;
  }

  zx::pager pager_;
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  zx::vmo pager_vmo_;
  async::PagedVmoMethod<MockUserPager, &MockUserPager::PageRequestHandler> page_request_handler_;
  std::atomic<int> num_page_faults_ = 0;
};

class MockPartitionClient : public FakePartitionClient {
 public:
  MockPartitionClient(MockUserPager* pager, size_t block_count)
      : FakePartitionClient(block_count), pager_(pager) {}

  // Writes the |vmo| to the partition, and verifies that no page faults are generated, i.e. the
  // |vmo| passed in is already populated.
  zx_status_t Write(const zx::vmo& vmo, size_t vmo_size) override {
    EXPECT_NOT_NULL(pager_);

    // The payload vmo was pager-backed. Verify that we saw some page faults to populate it.
    int page_faults_before = pager_->GetNumPageFaults();
    EXPECT_GT(page_faults_before, 0);

    // Issue the operation to write out the vmo to the partition.
    EXPECT_OK(FakePartitionClient::Write(vmo, vmo_size));

    // The write partition operation above should not trigger any further page faults.
    int page_faults_during = pager_->GetNumPageFaults() - page_faults_before;
    EXPECT_EQ(page_faults_during, 0);

    // Verify that we wrote out the partition correctly.
    fzl::VmoMapper mapper;
    EXPECT_OK(mapper.Map(partition_, 0, vmo_size, ZX_VM_PERM_READ));
    const uint8_t* start = reinterpret_cast<uint8_t*>(mapper.start());
    for (size_t i = 0; i < vmo_size; i++) {
      EXPECT_EQ(start[i], kData, "i = %zu", i);
    }
    return ZX_OK;
  }

 private:
  MockUserPager* pager_;
};

class MockDevicePartitioner : public FakeDevicePartitioner {
 public:
  explicit MockDevicePartitioner(MockUserPager* pager) : pager_(pager) {}

  // Dummy FindPartition function that creates and returns a MockPartitionClient.
  zx_status_t FindPartition(const paver::PartitionSpec& spec,
                            std::unique_ptr<paver::PartitionClient>* out_partition) const override {
    *out_partition = std::make_unique<MockPartitionClient>(pager_, kBlockCount);
    return ZX_OK;
  }

 private:
  MockUserPager* pager_;
};

// Test that verifies that DataSinkImpl::WriteAsset() populates a pager-backed vmo passed in as
// payload, before using it to write out a partition.
TEST(DataSinkTest, WriteAssetPaged) {
  MockUserPager pager;
  auto partitioner = std::make_unique<MockDevicePartitioner>(&pager);
  ASSERT_NE(partitioner.get(), nullptr);

  auto data_sink = paver::DataSinkImpl(fbl::unique_fd(), std::move(partitioner));

  ::llcpp::fuchsia::mem::Buffer payload;
  pager.CreatePayloadPaged(kPageCount, &payload);

  // The Configuration and Asset type passed in here are not relevant. They just need to be valid
  // values.
  ASSERT_NO_FATAL_FAILURES(data_sink.WriteAsset(::llcpp::fuchsia::paver::Configuration::A,
                                                ::llcpp::fuchsia::paver::Asset::KERNEL,
                                                std::move(payload)));
}

}  // namespace
