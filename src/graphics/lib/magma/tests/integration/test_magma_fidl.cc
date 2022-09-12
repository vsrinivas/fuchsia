// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.gpu.magma/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <unistd.h>

#include <filesystem>

#include <gtest/gtest.h>
#include <src/lib/fsl/handles/object_info.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

#include "test_magma.h"

namespace {

inline uint64_t page_size() { return sysconf(_SC_PAGESIZE); }

}  // namespace

// Magma clients are expected to use the libmagma client library, but the FIDL interface
// should be fully specified.  These tests ensure that.
//

using DeviceClient = fidl::WireSyncClient<fuchsia_gpu_magma::CombinedDevice>;
using PrimaryClient = fidl::WireClient<fuchsia_gpu_magma::Primary>;

class TestAsyncHandler : public fidl::WireAsyncEventHandler<fuchsia_gpu_magma::Primary> {
 public:
  void on_fidl_error(::fidl::UnbindInfo info) override { unbind_info_ = info; }

  auto& unbind_info() { return unbind_info_; }

  void OnNotifyMessagesConsumed(
      ::fidl::WireEvent<::fuchsia_gpu_magma::Primary::OnNotifyMessagesConsumed>* event) override {
    messages_consumed_ += event->count;
  }

  void OnNotifyMemoryImported(
      ::fidl::WireEvent<::fuchsia_gpu_magma::Primary::OnNotifyMemoryImported>* event) override {
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
      ASSERT_FALSE(device_.is_valid()) << " More than one GPU device found, specify --vendor-id";

      auto endpoints = fidl::CreateEndpoints<fuchsia_gpu_magma::CombinedDevice>();
      ASSERT_TRUE(endpoints.is_ok());

      zx_status_t zx_status =
          fdio_service_connect(p.path().c_str(), endpoints->server.TakeChannel().release());
      ASSERT_EQ(ZX_OK, zx_status);

      device_ = DeviceClient(std::move(endpoints->client));

      {
        auto wire_result = device_->Query(fuchsia_gpu_magma::wire::QueryId::kVendorId);
        ASSERT_TRUE(wire_result.ok());

        ASSERT_TRUE(wire_result->value()->is_simple_result());
        vendor_id_ = wire_result->value()->simple_result();
      }

      if (gVendorId == 0 || vendor_id_ == gVendorId) {
        break;
      } else {
        device_ = {};
      }
    }

    ASSERT_TRUE(device_.client_end());

    {
      auto wire_result = device_->Query(fuchsia_gpu_magma::wire::QueryId::kMaximumInflightParams);
      ASSERT_TRUE(wire_result.ok());

      ASSERT_TRUE(wire_result->value()->is_simple_result());
      uint64_t params = wire_result->value()->simple_result();
      max_inflight_messages_ = static_cast<uint32_t>(params >> 32);
    }

    auto primary_endpoints = fidl::CreateEndpoints<fuchsia_gpu_magma::Primary>();
    ASSERT_TRUE(primary_endpoints.is_ok());

    auto notification_endpoints = fidl::CreateEndpoints<fuchsia_gpu_magma::Notification>();
    ASSERT_TRUE(notification_endpoints.is_ok());

    uint64_t client_id = 0xabcd;  // anything
    auto wire_result = device_->Connect2(client_id, std::move(primary_endpoints->server),
                                         std::move(notification_endpoints->server));
    ASSERT_TRUE(wire_result.ok());

    primary_ = PrimaryClient(std::move(primary_endpoints->client), dispatcher(), &async_handler_);
    ASSERT_TRUE(primary_.is_valid());

    notification_channel_ = std::move(notification_endpoints->client.channel());
  }

  void TearDown() override {}

  uint64_t vendor_id() { return vendor_id_; }

  bool CheckForUnbind() {
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)primary_.sync()->Flush();
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
    auto wire_response = device_->Query(fuchsia_gpu_magma::wire::QueryId::kVendorId);
    EXPECT_TRUE(wire_response.ok());
    EXPECT_TRUE(wire_response.value().value()->is_simple_result());
    EXPECT_FALSE(wire_response.value().value()->is_buffer_result());
  }
  {
    auto wire_response = device_->Query(fuchsia_gpu_magma::wire::QueryId::kDeviceId);
    EXPECT_TRUE(wire_response.ok());
    EXPECT_TRUE(wire_response.value().value()->is_simple_result());
    EXPECT_FALSE(wire_response.value().value()->is_buffer_result());
  }
  {
    auto wire_response = device_->Query(fuchsia_gpu_magma::wire::QueryId::kIsTotalTimeSupported);
    EXPECT_TRUE(wire_response.ok());
    EXPECT_TRUE(wire_response.value().value()->is_simple_result());
    EXPECT_FALSE(wire_response.value().value()->is_buffer_result());
  }
  {
    auto wire_response = device_->Query(fuchsia_gpu_magma::wire::QueryId::kMaximumInflightParams);
    EXPECT_TRUE(wire_response.ok());
    EXPECT_TRUE(wire_response.value().value()->is_simple_result());
    EXPECT_FALSE(wire_response.value().value()->is_buffer_result());
  }
}

TEST_F(TestMagmaFidl, DumpState) {
  // TODO: define dumpstate param in magma.fidl. Or for testing only (use inspect instead)?
  auto wire_result = device_->DumpState(0);
  EXPECT_TRUE(wire_result.ok());
}

TEST_F(TestMagmaFidl, GetIcdList) {
  auto wire_result = device_->GetIcdList();
  EXPECT_TRUE(wire_result.ok());
}

TEST_F(TestMagmaFidl, ImportObjectInvalidType) {
  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, zx::vmo::create(4 /*size*/, 0 /*options*/, &vmo));
  constexpr auto kInvalidObjectType = fuchsia_gpu_magma::ObjectType(1000);
  auto wire_result = primary_->ImportObject2(std::move(vmo), kInvalidObjectType, /*id=*/1);
  EXPECT_TRUE(wire_result.ok());
  EXPECT_TRUE(CheckForUnbind());
}

TEST_F(TestMagmaFidl, ImportReleaseBuffer) {
  uint64_t buffer_id;

  {
    zx::vmo vmo;
    ASSERT_EQ(ZX_OK, zx::vmo::create(4 /*size*/, 0 /*options*/, &vmo));
    buffer_id = fsl::GetKoid(vmo.get());
    auto wire_result = primary_->ImportObject2(
        std::move(vmo), fuchsia_gpu_magma::wire::ObjectType::kBuffer, buffer_id);
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
    auto wire_result = primary_->ImportObject2(
        std::move(event), fuchsia_gpu_magma::wire::ObjectType::kEvent, event_id);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    auto wire_result =
        primary_->ReleaseObject(event_id, fuchsia_gpu_magma::wire::ObjectType::kEvent);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    uint64_t kBadId = event_id + 1;
    auto wire_result = primary_->ReleaseObject(kBadId, fuchsia_gpu_magma::wire::ObjectType::kEvent);
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
    uint32_t kBadId = context_id + 1;
    auto wire_result = primary_->DestroyContext(kBadId);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_TRUE(CheckForUnbind());
  }
}

TEST_F(TestMagmaFidl, MapUnmap) {
  fuchsia_gpu_magma::wire::BufferRange range;

  {
    zx::vmo vmo;
    ASSERT_EQ(ZX_OK, zx::vmo::create(4 /*size*/, 0 /*options*/, &vmo));

    uint64_t length;
    ASSERT_EQ(ZX_OK, vmo.get_size(&length));

    range = {.buffer_id = fsl::GetKoid(vmo.get()), .offset = 0, .size = length};

    auto wire_result = primary_->ImportObject2(
        std::move(vmo), fuchsia_gpu_magma::wire::ObjectType::kBuffer, range.buffer_id);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  constexpr uint64_t kGpuAddress = 0x1000;

  {
    constexpr fuchsia_gpu_magma::wire::MapFlags flags =
        fuchsia_gpu_magma::wire::MapFlags::kRead | fuchsia_gpu_magma::wire::MapFlags::kWrite |
        fuchsia_gpu_magma::wire::MapFlags::kExecute | fuchsia_gpu_magma::wire::MapFlags::kGrowable;

    fidl::Arena allocator;
    auto builder = fuchsia_gpu_magma::wire::PrimaryMapBufferRequest::Builder(allocator);
    builder.hw_va(kGpuAddress).range(range).flags(flags);

    auto wire_result = primary_->MapBuffer(builder.Build());
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    fidl::Arena allocator;
    auto builder = fuchsia_gpu_magma::wire::PrimaryUnmapBufferRequest::Builder(allocator);
    builder.hw_va(kGpuAddress).buffer_id(range.buffer_id);

    auto wire_result = primary_->UnmapBuffer(builder.Build());
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
TEST_F(TestMagmaFidl, ExecuteCommand) {
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
    auto wire_result = primary_->ImportObject2(
        std::move(vmo), fuchsia_gpu_magma::wire::ObjectType::kBuffer, buffer_id);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    fuchsia_gpu_magma::wire::BufferRange resource = {
        .buffer_id = buffer_id, .offset = 0, .size = 0};
    std::vector<fuchsia_gpu_magma::wire::BufferRange> resources{std::move(resource)};
    std::vector<fuchsia_gpu_magma::wire::CommandBuffer> command_buffers{{
        .resource_index = 0,
        .start_offset = 0,
    }};
    std::vector<uint64_t> wait_semaphores;
    std::vector<uint64_t> signal_semaphores;
    auto wire_result = primary_->ExecuteCommand(
        context_id, fidl::VectorView<fuchsia_gpu_magma::wire::BufferRange>::FromExternal(resources),
        fidl::VectorView<fuchsia_gpu_magma::wire::CommandBuffer>::FromExternal(command_buffers),
        fidl::VectorView<uint64_t>::FromExternal(wait_semaphores),
        fidl::VectorView<uint64_t>::FromExternal(signal_semaphores),
        fuchsia_gpu_magma::wire::CommandBufferFlags(0));
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

TEST_F(TestMagmaFidl, BufferRangeOp2) {
  // Not implemented for Intel or VSI
  if (vendor_id() == 0x8086 || vendor_id() == 0x10001) {
    GTEST_SKIP();
  }

  constexpr uint64_t kPageCount = 10;
  uint64_t size = kPageCount * page_size();
  uint64_t buffer_id;
  zx::vmo vmo;
  fuchsia_gpu_magma::wire::BufferRange range;

  {
    ASSERT_EQ(ZX_OK, zx::vmo::create(size, 0 /*options*/, &vmo));
    buffer_id = fsl::GetKoid(vmo.get());

    zx::vmo vmo_dupe;
    ASSERT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dupe));

    auto wire_result = primary_->ImportObject2(
        std::move(vmo_dupe), fuchsia_gpu_magma::wire::ObjectType::kBuffer, buffer_id);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());

    range = {.buffer_id = fsl::GetKoid(vmo.get()), .offset = 0, .size = size};
  }

  {
    zx_info_vmo_t info;
    ASSERT_EQ(ZX_OK, vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(0u, info.committed_bytes);
  }

  {
    fidl::Arena allocator;
    auto builder = fuchsia_gpu_magma::wire::PrimaryMapBufferRequest::Builder(allocator);
    builder.hw_va(0x1000).range(range).flags(fuchsia_gpu_magma::wire::MapFlags::kRead);

    auto wire_result = primary_->MapBuffer(builder.Build());
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  {
    auto wire_result =
        primary_->BufferRangeOp2(fuchsia_gpu_magma::wire::BufferOp::kPopulateTables, range);
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
    auto wire_result =
        primary_->BufferRangeOp2(fuchsia_gpu_magma::wire::BufferOp::kDepopulateTables, range);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_FALSE(CheckForUnbind());
  }

  // Depopulate doesn't decommit
  {
    zx_info_vmo_t info;
    ASSERT_EQ(ZX_OK, vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(size, info.committed_bytes);
  }

  // Check invalid range op
  {
    constexpr auto kInvalidBufferRangeOp = fuchsia_gpu_magma::BufferOp(1000);
    auto wire_result = primary_->BufferRangeOp2(kInvalidBufferRangeOp, range);
    EXPECT_TRUE(wire_result.ok());
    EXPECT_TRUE(CheckForUnbind());
  }
}

TEST_F(TestMagmaFidl, FlowControl) {
  // Without flow control, this will trigger a policy exception (too many channel messages)
  // or an OOM.
  auto result = primary_->EnableFlowControl();
  ZX_ASSERT(result.ok());

  constexpr uint32_t kIterations = 10000 / 2;

  int64_t messages_inflight = 0;

  for (uint32_t i = 0; i < kIterations; i++) {
    uint64_t buffer_id;
    {
      zx::vmo vmo;
      ASSERT_EQ(ZX_OK, zx::vmo::create(4 /*size*/, 0 /*options*/, &vmo));
      buffer_id = fsl::GetKoid(vmo.get());
      auto wire_result = primary_->ImportObject2(
          std::move(vmo), fuchsia_gpu_magma::wire::ObjectType::kBuffer, buffer_id);
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
      auto endpoints = fidl::CreateEndpoints<fuchsia_gpu_magma::PerformanceCounterAccess>();
      ASSERT_TRUE(endpoints.is_ok());
      ASSERT_EQ(ZX_OK,
                fdio_service_connect(p.path().c_str(), endpoints->server.TakeChannel().release()));
      perf_counter_access = fidl::WireSyncClient(std::move(endpoints->client));
    }

    zx::event access_token;

    {
      auto wire_result = perf_counter_access->GetPerformanceCountToken();
      ASSERT_TRUE(wire_result.ok());
      access_token = std::move(wire_result->access_token);
    }

    {
      auto wire_result = primary_->EnablePerformanceCounterAccess(std::move(access_token));
      ASSERT_TRUE(wire_result.ok());
    }

    {
      auto wire_result = primary_.sync()->IsPerformanceCounterAccessAllowed();
      ASSERT_TRUE(wire_result.ok());
      // Should be enabled if the gpu-performance-counters device matches the device under test
      if (wire_result->enabled) {
        success = true;
        break;
      }
    }
  }
  EXPECT_TRUE(success);
}
