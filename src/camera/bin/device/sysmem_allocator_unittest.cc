// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/sysmem_allocator.h"

#include <fuchsia/sysmem/cpp/fidl_test_base.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace camera {
namespace {

class FakeBufferCollection : public fuchsia::sysmem::testing::BufferCollection_TestBase {
 public:
  FakeBufferCollection(fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> request)
      : binding_{this, std::move(request)} {}

  void PeerClose() { binding_.Unbind(); }
  void CompleteBufferAllocation(zx_status_t status,
                                fuchsia::sysmem::BufferCollectionInfo_2 collection) {
    ASSERT_TRUE(allocated_callback_);
    allocated_callback_(status, std::move(collection));
    allocated_callback_ = nullptr;
  }

 private:
  // |fuchsia::sysmem::BufferCollection|
  void Close() override { binding_.Close(ZX_OK); }
  void SetName(uint32_t priority, std::string name) override {}
  void SetConstraints(bool has_constraints,
                      fuchsia::sysmem::BufferCollectionConstraints constraints) override {}
  void WaitForBuffersAllocated(WaitForBuffersAllocatedCallback callback) override {
    allocated_callback_ = std::move(callback);
  }

  void AttachLifetimeTracking(zx::eventpair server_end, uint32_t buffers_remaining) override {
    lifetime_tracking_ = std::move(server_end);
  }

  void NotImplemented_(const std::string& name) override {
    FAIL() << "Not Implemented BufferCollection." << name;
  }

  fidl::Binding<fuchsia::sysmem::BufferCollection> binding_;
  WaitForBuffersAllocatedCallback allocated_callback_;
  zx::eventpair lifetime_tracking_;
};

class FakeAllocator : public fuchsia::sysmem::testing::Allocator_TestBase {
 public:
  fuchsia::sysmem::AllocatorHandle NewBinding() { return binding_.NewBinding(); }

  const std::vector<std::unique_ptr<FakeBufferCollection>>& bound_collections() const {
    return collections_;
  }

 private:
  // |fuchsia::sysmem::Allocator|
  void AllocateNonSharedCollection(
      fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> request) override {
    collections_.emplace_back(std::make_unique<FakeBufferCollection>(std::move(request)));
  }
  void BindSharedCollection(
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
      fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection> request) override {
    collections_.emplace_back(std::make_unique<FakeBufferCollection>(std::move(request)));
  }

  void NotImplemented_(const std::string& name) override {
    FAIL() << "Not Implemented Allocator." << name;
  }

  fidl::Binding<fuchsia::sysmem::Allocator> binding_{this};
  std::vector<std::unique_ptr<FakeBufferCollection>> collections_;
};

class SysmemAllocatorTest : public gtest::TestLoopFixture {
 protected:
  FakeBufferCollection* last_bound_collection() {
    return sysmem_allocator_.bound_collections().empty()
               ? nullptr
               : sysmem_allocator_.bound_collections().back().get();
  }

  FakeAllocator sysmem_allocator_;
  SysmemAllocator allocator_{sysmem_allocator_.NewBinding()};
  async::Executor executor_{dispatcher()};
};

TEST_F(SysmemAllocatorTest, BindSharedCollection) {
  fuchsia::sysmem::BufferCollectionTokenHandle token;
  auto request = token.NewRequest();
  fpromise::result<BufferCollectionWithLifetime, zx_status_t> result;
  executor_.schedule_task(
      allocator_.BindSharedCollection(std::move(token), {}, "CollectionName")
          .then([&result](fpromise::result<BufferCollectionWithLifetime, zx_status_t>& r) mutable {
            result = std::move(r);
          }));
  RunLoopUntilIdle();

  // Now we should have the shared buffer collection.
  ASSERT_EQ(1u, sysmem_allocator_.bound_collections().size());
  last_bound_collection()->CompleteBufferAllocation(ZX_OK,
                                                    fuchsia::sysmem::BufferCollectionInfo_2{});
  RunLoopUntilIdle();
  EXPECT_TRUE(result.is_ok());
}

}  // namespace
}  // namespace camera
