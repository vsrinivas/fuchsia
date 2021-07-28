// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/magma/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <unistd.h>

#include <filesystem>

#include <gtest/gtest.h>
#include <src/lib/fsl/handles/object_info.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

#include "test_magma_abi.h"

namespace {

inline uint64_t page_size() { return sysconf(_SC_PAGESIZE); }

}  // namespace

// Magma clients are expected to use the libmagma client library, but the FIDL interface
// should be fully specified.  These tests ensure that.
//

using DeviceClient = fidl::WireSyncClient<fuchsia_gpu_magma::Device>;
using PrimaryClient = fidl::WireClient<fuchsia_gpu_magma::Primary>;

class TestAsyncHandler : public fidl::WireAsyncEventHandler<fuchsia_gpu_magma::Primary> {
 public:
  void on_fidl_error(::fidl::UnbindInfo info) override { unbind_info_ = info; }

  auto& unbind_info() { return unbind_info_; }

  void OnNotifyMessagesConsumed(
      ::fidl::WireResponse<::fuchsia_gpu_magma::Primary::OnNotifyMessagesConsumed>* event)
      override {
    messages_consumed_ += event->count;
  }

  void OnNotifyMemoryImported(
      ::fidl::WireResponse<::fuchsia_gpu_magma::Primary::OnNotifyMemoryImported>* event) override {
    // noop
  }

  uint64_t get_messages_consumed_and_reset() {
    uint64_t count = messages_consumed_;
    messages_consumed_ = 0;
    return count;
  }

 private:
  std::optional<fidl::UnbindInfo> unbind_info_;
  uint64_t messages_consumed_ = 0;
};

class TestMagmaFidl : public gtest::RealLoopFixture {
 public:
  static constexpr const char* kDevicePathFuchsia = "/dev/class/gpu";

  void SetUp() override {
    for (auto& p : std::filesystem::directory_iterator(kDevicePathFuchsia)) {
      ASSERT_FALSE(device_.client_end()) << " More than one GPU device found, specify --vendor-id";

      zx::channel server_end, client_end;
      zx::channel::create(0, &server_end, &client_end);

      zx_status_t zx_status = fdio_service_connect(p.path().c_str(), server_end.release());
      ASSERT_EQ(ZX_OK, zx_status);

      device_ = DeviceClient(std::move(client_end));

      {
        auto wire_result =
            device_.Query2(static_cast<uint64_t>(fuchsia_gpu_magma::wire::QueryId::kVendorId));
        ASSERT_TRUE(wire_result.ok());

        vendor_id_ = wire_result.Unwrap()->result.response().result;
      }

      if (gVendorId == 0 || vendor_id_ == gVendorId) {
        break;
      } else {
        device_ = {};
      }
    }

    ASSERT_TRUE(device_.client_end());

    {
      auto wire_result = device_.Query2(
          static_cast<uint64_t>(fuchsia_gpu_magma::wire::QueryId::kMaximumInflightParams));
      ASSERT_TRUE(wire_result.ok());

      uint64_t params = wire_result.Unwrap()->result.response().result;
      max_inflight_messages_ = static_cast<uint32_t>(params >> 32);
    }

    uint64_t client_id = 0xabcd;  // anything
    auto wire_result = device_.Connect(client_id);
    ASSERT_TRUE(wire_result.ok());

    primary_ = PrimaryClient(std::move(wire_result.Unwrap()->primary_channel), dispatcher(),
                             &async_handler_);
    ASSERT_TRUE(primary_.is_valid());

    notification_channel_ = std::move(wire_result.Unwrap()->notification_channel);
  }

  void TearDown() override {}

  uint64_t vendor_id() { return vendor_id_; }

  bool CheckForUnbind() {
    primary_->Sync_Sync();
    RunLoopUntilIdle();
    return async_handler_.unbind_info().has_value();
  }

  DeviceClient device_;
  uint64_t vendor_id_ = 0;
  uint32_t max_inflight_messages_ = 0;
  TestAsyncHandler async_handler_;
  PrimaryClient primary_;
  zx::channel notification_channel_;
};

TEST_F(TestMagmaFidl, Connect) {
  // Just setup and teardown
}

TEST_F(TestMagmaFidl, Query) {
  {
    auto wire_result =
        device_.Query2(static_cast<uint64_t>(fuchsia_gpu_magma::wire::QueryId::kVendorId));
    EXPECT_TRUE(wire_result.ok());
  }
  {
    auto wire_result =
        device_.Query2(static_cast<uint64_t>(fuchsia_gpu_magma::wire::QueryId::kDeviceId));
    EXPECT_TRUE(wire_result.ok());
  }
  {
    auto wire_result = device_.Query2(
        static_cast<uint64_t>(fuchsia_gpu_magma::wire::QueryId::kIsTotalTimeSupported));
    EXPECT_TRUE(wire_result.ok());
  }
  {
    auto wire_result = device_.Query2(
        static_cast<uint64_t>(fuchsia_gpu_magma::wire::QueryId::kMaximumInflightParams));
    EXPECT_TRUE(wire_result.ok());
  }
}

TEST_F(TestMagmaFidl, QueryReturnsBuffer) {
  // No predefined queries return a buffer
  const uint64_t query_id = static_cast<uint64_t>(fuchsia_gpu_magma::wire::QueryId::kVendorId);
  auto wire_result = device_.QueryReturnsBuffer(query_id);
  EXPECT_TRUE(wire_result.ok());
  EXPECT_TRUE(wire_result.Unwrap()->result.is_err());
}

TEST_F(TestMagmaFidl, DumpState) {
  // TODO: define dumpstate param in magma.fidl. Or for testing only (use inspect instead)?
  auto wire_result = device_.DumpState(0);
  EXPECT_TRUE(wire_result.ok());
}

TEST_F(TestMagmaFidl, GetIcdList) {
  auto wire_result = device_.GetIcdList();
  EXPECT_TRUE(wire_result.ok());
}

TEST_F(TestMagmaFidl, ImportReleaseBuffer) {
  uint64_t buffer_id;

  {
    zx::vmo vmo;
    ASSERT_EQ(ZX_OK, zx::vmo::create(4 /*size*/, 0 /*options*/, &vmo));
    buffer_id = fsl::GetKoid(vmo.get());
    auto wire_result =
        primary_->ImportObject(std::move(vmo), fuchsia_gpu_magma::wire::ObjectType::kBuffer);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    auto wire_result =
        primary_->ReleaseObject(buffer_id, fuchsia_gpu_magma::wire::ObjectType::kBuffer);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    uint64_t kBadId = buffer_id + 1;
    auto wire_result =
        primary_->ReleaseObject(kBadId, fuchsia_gpu_magma::wire::ObjectType::kBuffer);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_TRUE(CheckForUnbind());
  }
}

TEST_F(TestMagmaFidl, ImportReleaseSemaphore) {
  uint64_t event_id;

  {
    zx::event event;
    ASSERT_EQ(ZX_OK, zx::event::create(0 /*options*/, &event));
    event_id = fsl::GetKoid(event.get());
    auto wire_result =
        primary_->ImportObject(std::move(event), fuchsia_gpu_magma::wire::ObjectType::kSemaphore);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    auto wire_result =
        primary_->ReleaseObject(event_id, fuchsia_gpu_magma::wire::ObjectType::kSemaphore);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    uint64_t kBadId = event_id + 1;
    auto wire_result =
        primary_->ReleaseObject(kBadId, fuchsia_gpu_magma::wire::ObjectType::kSemaphore);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_TRUE(CheckForUnbind());
  }
}

TEST_F(TestMagmaFidl, CreateDestroyContext) {
  uint32_t context_id = 10;

  {
    auto wire_result = primary_->CreateContext(context_id);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    auto wire_result = primary_->DestroyContext(context_id);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    uint64_t kBadId = context_id + 1;
    auto wire_result = primary_->DestroyContext(kBadId);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_TRUE(CheckForUnbind());
  }
}

TEST_F(TestMagmaFidl, MapUnmap) {
  uint64_t buffer_id;

  {
    zx::vmo vmo;
    ASSERT_EQ(ZX_OK, zx::vmo::create(4 /*size*/, 0 /*options*/, &vmo));
    buffer_id = fsl::GetKoid(vmo.get());
    auto wire_result =
        primary_->ImportObject(std::move(vmo), fuchsia_gpu_magma::wire::ObjectType::kBuffer);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  constexpr uint64_t kGpuAddress = 0x1000;

  {
    constexpr fuchsia_gpu_magma::wire::MapFlags flags =
        fuchsia_gpu_magma::wire::MapFlags::kRead | fuchsia_gpu_magma::wire::MapFlags::kWrite |
        fuchsia_gpu_magma::wire::MapFlags::kExecute | fuchsia_gpu_magma::wire::MapFlags::kGrowable;
    auto wire_result =
        primary_->MapBufferGpu(buffer_id, kGpuAddress, 0 /*page_offset*/, 1 /*page_count*/, flags);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    auto wire_result = primary_->UnmapBufferGpu(buffer_id, kGpuAddress);
    EXPECT_TRUE(wire_result.ok());
    // Unmap not implemented on Intel
    if (vendor_id() == 0x8086) {
      EXPECT_TRUE(CheckForUnbind());
    } else {
      EXPECT_FALSE(CheckForUnbind());
    }
  }
}

// Sends a bunch of zero command bytes
TEST_F(TestMagmaFidl, ExecuteCommandBufferWithResources) {
  uint32_t context_id = 10;

  {
    auto wire_result = primary_->CreateContext(context_id);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  uint64_t buffer_id;

  {
    zx::vmo vmo;
    ASSERT_EQ(ZX_OK, zx::vmo::create(4096, 0 /*options*/, &vmo));
    buffer_id = fsl::GetKoid(vmo.get());
    auto wire_result =
        primary_->ImportObject(std::move(vmo), fuchsia_gpu_magma::wire::ObjectType::kBuffer);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    fuchsia_gpu_magma::wire::Resource resource = {.buffer = buffer_id, .offset = 0, .length = 0};
    std::vector<fuchsia_gpu_magma::wire::Resource> resources{std::move(resource)};
    fuchsia_gpu_magma::wire::CommandBuffer command_buffer = {.batch_buffer_resource_index = 0,
                                                             .batch_start_offset = 0};
    std::vector<uint64_t> wait_semaphores;
    std::vector<uint64_t> signal_semaphores;
    auto wire_result = primary_->ExecuteCommandBufferWithResources(
        context_id, std::move(command_buffer),
        fidl::VectorView<fuchsia_gpu_magma::wire::Resource>::FromExternal(resources),
        fidl::VectorView<uint64_t>::FromExternal(wait_semaphores),
        fidl::VectorView<uint64_t>::FromExternal(signal_semaphores));
    EXPECT_TRUE(wire_result.ok());

    // Fails checking (resource not mapped), does not execute on GPU
    EXPECT_TRUE(CheckForUnbind());
  }
}

// Sends a bunch of zero command bytes
TEST_F(TestMagmaFidl, ExecuteImmediateCommands) {
  uint32_t context_id = 10;

  {
    auto wire_result = primary_->CreateContext(context_id);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    std::array<uint8_t, fuchsia_gpu_magma::wire::kMaxImmediateCommandsDataSize>
        command_bytes{};  // zero init
    std::vector<uint64_t> signal_semaphores;
    auto wire_result = primary_->ExecuteImmediateCommands(
        context_id, fidl::VectorView<uint8_t>::FromExternal(command_bytes),
        fidl::VectorView<uint64_t>::FromExternal(signal_semaphores));
    EXPECT_TRUE(wire_result.ok());

    // Fails checking, does not execute on GPU
    EXPECT_TRUE(CheckForUnbind());
  }
}

TEST_F(TestMagmaFidl, BufferRangeOp) {
  // Not implemented for Intel or VSI
  if (vendor_id() == 0x8086 || vendor_id() == 0x10001) {
    GTEST_SKIP();
  }

  constexpr uint64_t kPageCount = 10;
  uint64_t size = kPageCount * page_size();
  uint64_t buffer_id;
  zx::vmo vmo;

  {
    ASSERT_EQ(ZX_OK, zx::vmo::create(size, 0 /*options*/, &vmo));
    buffer_id = fsl::GetKoid(vmo.get());

    zx::vmo vmo_dupe;
    ASSERT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dupe));

    auto wire_result =
        primary_->ImportObject(std::move(vmo_dupe), fuchsia_gpu_magma::wire::ObjectType::kBuffer);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    zx_info_vmo_t info;
    ASSERT_EQ(ZX_OK, vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(0u, info.committed_bytes);
  }

  {
    auto wire_result = primary_->MapBufferGpu(buffer_id, 0x1000 /*gpu_address*/, 0 /*page_offset*/,
                                              kPageCount, fuchsia_gpu_magma::wire::MapFlags::kRead);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    auto wire_result = primary_->BufferRangeOp(
        buffer_id, fuchsia_gpu_magma::wire::BufferOp::kPopulateTables, 0 /*start_bytes*/, size);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  // Should be sync'd after the unbind check
  {
    zx_info_vmo_t info;
    ASSERT_EQ(ZX_OK, vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(size, info.committed_bytes);
  }

  {
    auto wire_result = primary_->BufferRangeOp(
        buffer_id, fuchsia_gpu_magma::wire::BufferOp::kDepopulateTables, 0 /*start_bytes*/, size);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  // Depopulate doesn't decommit
  {
    zx_info_vmo_t info;
    ASSERT_EQ(ZX_OK, vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(size, info.committed_bytes);
  }
}

TEST_F(TestMagmaFidl, Commit) {
  // Not implemented for Intel or VSI
  if (vendor_id() == 0x8086 || vendor_id() == 0x10001) {
    GTEST_SKIP();
  }

  constexpr uint64_t kPageCount = 10;
  uint64_t size = kPageCount * page_size();
  uint64_t buffer_id;
  zx::vmo vmo;

  {
    ASSERT_EQ(ZX_OK, zx::vmo::create(size, 0 /*options*/, &vmo));
    buffer_id = fsl::GetKoid(vmo.get());

    zx::vmo vmo_dupe;
    ASSERT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dupe));

    auto wire_result =
        primary_->ImportObject(std::move(vmo_dupe), fuchsia_gpu_magma::wire::ObjectType::kBuffer);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    auto wire_result = primary_->MapBufferGpu(buffer_id, 0x1000 /*gpu_address*/, 0 /*page_offset*/,
                                              kPageCount, fuchsia_gpu_magma::wire::MapFlags::kRead);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    zx_info_vmo_t info;
    ASSERT_EQ(ZX_OK, vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(0u, info.committed_bytes);
  }

  {
    auto wire_result = primary_->CommitBuffer(buffer_id, 0 /*page_offset*/, kPageCount);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  // Should be sync'd after the unbind check
  {
    zx_info_vmo_t info;
    ASSERT_EQ(ZX_OK, vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(size, info.committed_bytes);
  }
}

TEST_F(TestMagmaFidl, FlowControl) {
  // Without flow control, this will trigger a policy exception (too many channel messages)
  // or an OOM.
  primary_->EnableFlowControl();

  constexpr uint32_t kIterations = 10000 / 2;

  int64_t messages_inflight = 0;

  for (uint32_t i = 0; i < kIterations; i++) {
    uint64_t buffer_id;
    {
      zx::vmo vmo;
      ASSERT_EQ(ZX_OK, zx::vmo::create(4 /*size*/, 0 /*options*/, &vmo));
      buffer_id = fsl::GetKoid(vmo.get());
      auto wire_result =
          primary_->ImportObject(std::move(vmo), fuchsia_gpu_magma::wire::ObjectType::kBuffer);
      EXPECT_TRUE(wire_result.ok());
    }

    {
      auto wire_result =
          primary_->ReleaseObject(buffer_id, fuchsia_gpu_magma::wire::ObjectType::kBuffer);
      EXPECT_TRUE(wire_result.ok());
    }

    messages_inflight += 2;

    if (messages_inflight < max_inflight_messages_)
      continue;

    RunLoopUntil([&messages_inflight, this]() {
      uint64_t count = async_handler_.get_messages_consumed_and_reset();
      if (count) {
        messages_inflight -= count;
        EXPECT_GE(messages_inflight, 0u);
      }
      return messages_inflight < max_inflight_messages_;
    });
  }
}

TEST_F(TestMagmaFidl, EnablePerformanceCounters) {
  bool success = false;
  for (auto& p : std::filesystem::directory_iterator("/dev/class/gpu-performance-counters")) {
    fidl::WireSyncClient<fuchsia_gpu_magma::PerformanceCounterAccess> perf_counter_access;

    {
      zx::channel server_end, client_end;
      ASSERT_EQ(ZX_OK, zx::channel::create(0, &server_end, &client_end));
      ASSERT_EQ(ZX_OK, fdio_service_connect(p.path().c_str(), server_end.release()));
      perf_counter_access =
          fidl::WireSyncClient<fuchsia_gpu_magma::PerformanceCounterAccess>(std::move(client_end));
    }

    zx::event access_token;

    {
      auto wire_result = perf_counter_access.GetPerformanceCountToken();
      ASSERT_TRUE(wire_result.ok());
      access_token = std::move(wire_result.Unwrap()->access_token);
    }

    {
      auto wire_result = primary_->AccessPerformanceCounters(std::move(access_token));
      ASSERT_TRUE(wire_result.ok());
    }

    {
      auto wire_result = primary_->IsPerformanceCounterAccessEnabled_Sync();
      ASSERT_TRUE(wire_result.ok());
      // Should be enabled if the gpu-performance-counters device matches the device under test
      if (wire_result.Unwrap()->enabled) {
        success = true;
        break;
      }
    }
  }
  EXPECT_TRUE(success);
}
