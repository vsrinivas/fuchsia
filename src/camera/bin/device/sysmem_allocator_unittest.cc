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

constexpr std::array<zx::duration, 10> kTimeoutByAttempt = {{
    zx::msec(200),
    zx::msec(288),
    zx::msec(377),
    zx::msec(466),
    zx::msec(555),
    zx::msec(644),
    zx::msec(733),
    zx::msec(822),
    zx::msec(911),
    zx::msec(1000),
}};

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

  void NotImplemented_(const std::string& name) override {
    FAIL() << "Not Implemented BufferCollection." << name;
  }

  fidl::Binding<fuchsia::sysmem::BufferCollection> binding_;
  WaitForBuffersAllocatedCallback allocated_callback_;
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
  SysmemAllocator allocator_{dispatcher(), sysmem_allocator_.NewBinding()};
  async::Executor executor_{dispatcher()};
};

TEST_F(SysmemAllocatorTest, SafelyBindSharedCollection) {
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
  auto request = token.NewRequest();
  fit::result<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t> result;
  executor_.schedule_task(
      allocator_.SafelyBindSharedCollection(std::move(token), {}, "CollectionName")
          .then([&result](
                    fit::result<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t>& r) mutable {
            result = std::move(r);
          }));
  RunLoopUntilIdle();

  // First buffer collection to complete, this is for the initial free-space 'probe'
  ASSERT_EQ(1u, sysmem_allocator_.bound_collections().size());
  last_bound_collection()->CompleteBufferAllocation(ZX_OK,
                                                    fuchsia::sysmem::BufferCollectionInfo_2{});
  RunLoopFor(kTimeoutByAttempt[0]);

  // Now we should have the shared buffer collection.
  ASSERT_EQ(2u, sysmem_allocator_.bound_collections().size());
  last_bound_collection()->CompleteBufferAllocation(ZX_OK,
                                                    fuchsia::sysmem::BufferCollectionInfo_2{});
  RunLoopUntilIdle();
  EXPECT_TRUE(result.is_ok());
}

TEST_F(SysmemAllocatorTest, SafelyBindSharedCollectionNoMemory) {
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
  auto request = token.NewRequest();
  fit::result<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t> result;
  executor_.schedule_task(
      allocator_.SafelyBindSharedCollection(std::move(token), {}, "CollectionName")
          .then([&result](
                    fit::result<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t>& r) mutable {
            result = std::move(r);
          }));
  RunLoopUntilIdle();

  for (size_t i = 1; i <= 10; ++i) {
    EXPECT_TRUE(result.is_pending());
    ASSERT_EQ(i, sysmem_allocator_.bound_collections().size());
    last_bound_collection()->CompleteBufferAllocation(ZX_ERR_NO_MEMORY,
                                                      fuchsia::sysmem::BufferCollectionInfo_2{});
    RunLoopFor(kTimeoutByAttempt[i - 1]);
  }

  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), ZX_ERR_NO_MEMORY);
}

TEST_F(SysmemAllocatorTest, SafelyBindSharedCollectionPeerClosed) {
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
  auto request = token.NewRequest();
  fit::result<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t> result;
  executor_.schedule_task(
      allocator_.SafelyBindSharedCollection(std::move(token), {}, "CollectionName")
          .then([&result](
                    fit::result<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t>& r) mutable {
            result = std::move(r);
          }));
  RunLoopUntilIdle();

  for (size_t i = 1; i <= 10; ++i) {
    EXPECT_TRUE(result.is_pending());
    ASSERT_EQ(i, sysmem_allocator_.bound_collections().size());
    last_bound_collection()->PeerClose();
    RunLoopFor(kTimeoutByAttempt[i - 1]);
  }

  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), ZX_ERR_NO_MEMORY);
}

}  // namespace
}  // namespace camera
