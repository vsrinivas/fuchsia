// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/sync/completion.h>
#include <lib/zx/bti.h>
#include <stdlib.h>
#include <zircon/errors.h>

#include <ddktl/device.h>
#include <zxtest/zxtest.h>

#include "../buffer_collection.h"
#include "../device.h"
#include "../driver.h"
#include "../logical_buffer_collection.h"
#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"

namespace sysmem_driver {
namespace {
class FakePBus : public ddk::PBusProtocol<FakePBus, ddk::base_protocol> {
 public:
  FakePBus() : proto_({&pbus_protocol_ops_, this}) {}
  const pbus_protocol_t* proto() const { return &proto_; }
  zx_status_t PBusDeviceAdd(const pbus_dev_t* dev) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusRegisterProtocol(uint32_t proto_id, const uint8_t* protocol,
                                   size_t protocol_size) {
    registered_proto_id_ = proto_id;
    return ZX_OK;
  }
  zx_status_t PBusGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusSetBoardInfo(const pbus_board_info_t* info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusSetBootloaderInfo(const pbus_bootloader_info_t* info) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusCompositeDeviceAdd(const pbus_dev_t* dev,
                                     /* const device_fragment_t* */ uint64_t fragments_list,
                                     size_t fragments_count, const char* primary_fragment) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusAddComposite(const pbus_dev_t* dev,
                               /* const device_fragment_t* */ uint64_t fragments_list,
                               size_t fragments_count, const char* primary_fragment) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusRegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cbin) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint32_t registered_proto_id() const { return registered_proto_id_; }

 private:
  pbus_protocol_t proto_;
  uint32_t registered_proto_id_ = 0;
};

TEST(Device, OverrideCommandLine) {
  const char* kCommandLine = "test.device.commandline";

  int64_t value;
  zx_status_t status;

  value = 10;
  setenv(kCommandLine, "5", 1);
  status = Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_OK(status);
  EXPECT_EQ(5, value);

  value = 11;
  setenv(kCommandLine, "65537", 1);
  status = Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_OK(status);
  EXPECT_EQ(65537, value);

  // Trailing characters should cause the entire value to be ignored.
  value = 12;
  setenv(kCommandLine, "65536a", 1);
  status = Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
  EXPECT_EQ(12, value);

  // Empty values should be ignored.
  value = 13;
  setenv(kCommandLine, "", 1);
  status = Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_OK(status);
  EXPECT_EQ(13, value);

  // Negative values are allowed (these get interpreted as a percentage of physical RAM), but only
  // up to 99% is allowed.
  value = 14;
  setenv(kCommandLine, "-100", 1);
  status = Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
  EXPECT_EQ(14, value);

  value = 15;
  setenv(kCommandLine, "-99", 1);
  status = Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_OK(status);
  EXPECT_EQ(-99, value);
}

TEST(Device, GuardPageCommandLine) {
  uint64_t guard_bytes = 1;
  bool internal_guard_pages = true;
  bool crash_on_fail = true;
  const char* kName = "driver.sysmem.contiguous_guard_page_count";
  const char* kInternalName = "driver.sysmem.contiguous_guard_pages_internal";

  setenv(kInternalName, "", true);
  EXPECT_EQ(ZX_OK, Device::GetContiguousGuardParameters(&guard_bytes, &internal_guard_pages,
                                                        &crash_on_fail));
  EXPECT_EQ(zx_system_get_page_size(), guard_bytes);
  EXPECT_TRUE(internal_guard_pages);
  EXPECT_FALSE(crash_on_fail);
  unsetenv(kInternalName);

  setenv(kName, "fasfas", true);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, Device::GetContiguousGuardParameters(
                                     &guard_bytes, &internal_guard_pages, &crash_on_fail));
  EXPECT_EQ(zx_system_get_page_size(), guard_bytes);
  EXPECT_FALSE(internal_guard_pages);
  EXPECT_FALSE(crash_on_fail);

  setenv(kName, "", true);
  EXPECT_EQ(ZX_OK, Device::GetContiguousGuardParameters(&guard_bytes, &internal_guard_pages,
                                                        &crash_on_fail));
  EXPECT_EQ(zx_system_get_page_size(), guard_bytes);
  EXPECT_FALSE(internal_guard_pages);
  EXPECT_FALSE(crash_on_fail);

  setenv(kName, "2", true);
  setenv(kInternalName, "", true);
  EXPECT_EQ(ZX_OK, Device::GetContiguousGuardParameters(&guard_bytes, &internal_guard_pages,
                                                        &crash_on_fail));
  EXPECT_EQ(zx_system_get_page_size() * 2, guard_bytes);
  EXPECT_TRUE(internal_guard_pages);
  EXPECT_FALSE(crash_on_fail);

  const char* kFatalName = "driver.sysmem.contiguous_guard_pages_fatal";
  setenv(kFatalName, "", true);
  EXPECT_EQ(ZX_OK, Device::GetContiguousGuardParameters(&guard_bytes, &internal_guard_pages,
                                                        &crash_on_fail));
  EXPECT_EQ(zx_system_get_page_size() * 2, guard_bytes);
  EXPECT_TRUE(internal_guard_pages);
  EXPECT_TRUE(crash_on_fail);

  unsetenv(kName);
  unsetenv(kFatalName);
  unsetenv(kInternalName);
}

class FakeDdkSysmem : public zxtest::Test {
 public:
  void SetUp() override {
    pdev_.UseFakeBti();
    ddk_.SetProtocol(ZX_PROTOCOL_PDEV, pdev_.proto());
    EXPECT_EQ(sysmem_.Bind(), ZX_OK);
  }

  void TearDown() override {
    sysmem_.DdkAsyncRemove();
    EXPECT_OK(ddk_.WaitUntilRemove());
    EXPECT_TRUE(ddk_.Ok());
  }

  fidl::ClientEnd<fuchsia_sysmem::Allocator> Connect() {
    zx::status allocator_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::Allocator>();
    EXPECT_OK(allocator_endpoints);
    auto [allocator_client_end, allocator_server_end] = std::move(*allocator_endpoints);

    fidl::WireResult result =
        fidl::WireCall(
            fidl::UnownedClientEnd<fuchsia_sysmem::DriverConnector>(zx::unowned(ddk_.FidlClient())))
            .Connect(std::move(allocator_server_end));
    EXPECT_OK(result);
    return std::move(allocator_client_end);
  }

  fidl::ClientEnd<fuchsia_sysmem::BufferCollection> AllocateNonSharedCollection() {
    fidl::WireSyncClient<fuchsia_sysmem::Allocator> allocator(Connect());

    zx::status collection_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
    EXPECT_OK(collection_endpoints);
    auto [collection_client_end, collection_server_end] = std::move(*collection_endpoints);

    EXPECT_OK(allocator.AllocateNonSharedCollection(std::move(collection_server_end)));
    return std::move(collection_client_end);
  }

 protected:
  sysmem_driver::Driver sysmem_ctx_;
  sysmem_driver::Device sysmem_{fake_ddk::kFakeParent, &sysmem_ctx_};

  fake_pdev::FakePDev pdev_;
  // ddk must be destroyed before sysmem because it may be executing messages against sysmem on
  // another thread.
  fake_ddk::Bind ddk_;
};

class FakeDdkSysmemPbus : public FakeDdkSysmem {
 public:
  void SetUp() override {
    pdev_.UseFakeBti();
    ddk_.SetProtocol(ZX_PROTOCOL_PBUS, pbus_.proto());
    ddk_.SetProtocol(ZX_PROTOCOL_PDEV, pdev_.proto());
    EXPECT_EQ(sysmem_.Bind(), ZX_OK);
  }

 protected:
  FakePBus pbus_;
};

TEST_F(FakeDdkSysmem, TearDownLoop) {
  // Queue up something that would be processed on the FIDL thread, so we can try to detect a
  // use-after-free if the FidlServer outlives the sysmem device.
  AllocateNonSharedCollection();
}

// Test that creating and tearing down a SecureMem connection works correctly.
TEST_F(FakeDdkSysmem, DummySecureMem) {
  zx::channel securemem_server, securemem_client;
  ASSERT_OK(zx::channel::create(0u, &securemem_server, &securemem_client));
  EXPECT_EQ(ZX_OK, sysmem_.SysmemRegisterSecureMem(std::move(securemem_server)));

  // This shouldn't deadlock waiting for a message on the channel.
  EXPECT_EQ(ZX_OK, sysmem_.SysmemUnregisterSecureMem());

  // This shouldn't cause a panic due to receiving peer closed.
  securemem_client.reset();
}

TEST_F(FakeDdkSysmem, NamedToken) {
  fidl::WireSyncClient<fuchsia_sysmem::Allocator> allocator(Connect());

  zx::status token_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  EXPECT_OK(token_endpoints);
  auto [token_client_end, token_server_end] = std::move(*token_endpoints);

  EXPECT_OK(allocator.AllocateSharedCollection(std::move(token_server_end)));

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollectionToken> token(std::move(token_client_end));

  // The buffer collection should end up with a name of "a" because that's the highest priority.
  EXPECT_OK(token.SetName(5u, "c"));
  EXPECT_OK(token.SetName(100u, "a"));
  EXPECT_OK(token.SetName(6u, "b"));

  zx::status collection_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  EXPECT_OK(collection_endpoints);
  auto [collection_client_end, collection_server_end] = std::move(*collection_endpoints);

  EXPECT_OK(allocator.BindSharedCollection(std::move(token.client_end()),
                                           std::move(collection_server_end)));

  // Poll until a matching buffer collection is found.
  while (true) {
    bool found_collection = false;
    sync_completion_t completion;
    async::PostTask(sysmem_.dispatcher(), [&] {
      if (sysmem_.logical_buffer_collections().size() == 1) {
        const auto* logical_collection = *sysmem_.logical_buffer_collections().begin();
        auto collection_views = logical_collection->collection_views();
        if (collection_views.size() == 1) {
          auto name = logical_collection->name();
          EXPECT_TRUE(name);
          EXPECT_EQ("a", *name);
          found_collection = true;
        }
      }
      sync_completion_signal(&completion);
    });

    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    if (found_collection)
      break;
  }
}

TEST_F(FakeDdkSysmem, NamedClient) {
  auto collection_client_end = AllocateNonSharedCollection();

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(
      std::move(collection_client_end));
  EXPECT_OK(collection.SetDebugClientInfo("a", 5));

  // Poll until a matching buffer collection is found.
  while (true) {
    bool found_collection = false;
    sync_completion_t completion;
    async::PostTask(sysmem_.dispatcher(), [&] {
      if (sysmem_.logical_buffer_collections().size() == 1) {
        const auto* logical_collection = *sysmem_.logical_buffer_collections().begin();
        if (logical_collection->collection_views().size() == 1) {
          const BufferCollection* collection = logical_collection->collection_views().front();
          if (collection->node_properties().client_debug_info().name == "a") {
            EXPECT_EQ(5u, collection->node_properties().client_debug_info().id);
            found_collection = true;
          }
        }
      }
      sync_completion_signal(&completion);
    });

    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    if (found_collection)
      break;
  }
}

// Check that the allocator name overrides the collection name.
TEST_F(FakeDdkSysmem, NamedAllocatorToken) {
  fidl::WireSyncClient<fuchsia_sysmem::Allocator> allocator(Connect());

  zx::status token_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  EXPECT_OK(token_endpoints);
  auto [token_client_end, token_server_end] = std::move(*token_endpoints);

  EXPECT_OK(allocator.AllocateSharedCollection(std::move(token_server_end)));

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollectionToken> token(std::move(token_client_end));

  EXPECT_OK(token.SetDebugClientInfo("bad", 6));
  EXPECT_OK(allocator.SetDebugClientInfo("a", 5));

  zx::status collection_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  EXPECT_OK(collection_endpoints);
  auto [collection_client_end, collection_server_end] = std::move(*collection_endpoints);

  EXPECT_OK(allocator.BindSharedCollection(std::move(token.client_end()),
                                           std::move(collection_server_end)));

  // Poll until a matching buffer collection is found.
  while (true) {
    bool found_collection = false;
    sync_completion_t completion;
    async::PostTask(sysmem_.dispatcher(), [&] {
      if (sysmem_.logical_buffer_collections().size() == 1) {
        const auto* logical_collection = *sysmem_.logical_buffer_collections().begin();
        auto collection_views = logical_collection->collection_views();
        if (collection_views.size() == 1) {
          const auto& collection = collection_views.front();
          if (collection->node_properties().client_debug_info().name == "a") {
            EXPECT_EQ(5u, collection->node_properties().client_debug_info().id);
            found_collection = true;
          }
        }
      }
      sync_completion_signal(&completion);
    });

    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    if (found_collection)
      break;
  }
}

TEST_F(FakeDdkSysmem, MaxSize) {
  sysmem_.set_settings(sysmem_driver::Settings{.max_allocation_size = zx_system_get_page_size()});

  auto collection_client = AllocateNonSharedCollection();

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.min_size_bytes = zx_system_get_page_size() * 2;
  constraints.buffer_memory_constraints.cpu_domain_supported = true;
  constraints.usage.cpu = fuchsia_sysmem_cpuUsageRead;

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(std::move(collection_client));
  EXPECT_OK(collection.SetConstraints(true, std::move(constraints)));

  // Sysmem should fail the collection and return an error.
  fidl::WireResult result = collection.WaitForBuffersAllocated();
  EXPECT_NE(result.status(), ZX_OK);
}

// Check that teardown doesn't leak any memory (detected through LSAN).
TEST_F(FakeDdkSysmem, TeardownLeak) {
  auto collection_client = AllocateNonSharedCollection();

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.min_size_bytes = zx_system_get_page_size();
  constraints.buffer_memory_constraints.cpu_domain_supported = true;
  constraints.usage.cpu = fuchsia_sysmem_cpuUsageRead;

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(std::move(collection_client));
  EXPECT_OK(collection.SetConstraints(true, std::move(constraints)));

  fidl::WireResult result = collection.WaitForBuffersAllocated();

  EXPECT_OK(result);
  EXPECT_OK(result->status);

  for (uint32_t i = 0; i < result->buffer_collection_info.buffer_count; i++) {
    result->buffer_collection_info.buffers[i].vmo.reset();
  }
  collection.client_end().reset();
}

// Check that there are no circular references from a VMO to the logical buffer collection, even
// when aux buffers are checked for.
TEST_F(FakeDdkSysmem, AuxBufferLeak) {
  auto collection_client = AllocateNonSharedCollection();

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.min_size_bytes = zx_system_get_page_size();
  constraints.buffer_memory_constraints.cpu_domain_supported = true;
  constraints.usage.cpu = fuchsia_sysmem_cpuUsageRead;

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(std::move(collection_client));
  EXPECT_OK(collection.SetConstraints(true, std::move(constraints)));

  fidl::WireResult result = collection.WaitForBuffersAllocated();

  EXPECT_OK(result);
  EXPECT_OK(result->status);

  for (uint32_t i = 0; i < result->buffer_collection_info.buffer_count; i++) {
    result->buffer_collection_info.buffers[i].vmo.reset();
  }

  fidl::WireResult aux_result = collection.GetAuxBuffers();

  EXPECT_OK(aux_result);
  EXPECT_OK(aux_result->status);
  EXPECT_EQ(1u, aux_result->buffer_collection_info_aux_buffers.buffer_count);
  EXPECT_EQ(ZX_HANDLE_INVALID, aux_result->buffer_collection_info_aux_buffers.buffers[0].vmo);
  collection.client_end().reset();

  // Poll until all buffer collections are deleted.
  while (true) {
    bool no_collections = false;
    sync_completion_t completion;
    async::PostTask(sysmem_.dispatcher(), [&] {
      no_collections = sysmem_.logical_buffer_collections().empty();
      sync_completion_signal(&completion);
    });

    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    if (no_collections)
      break;
  }
}

TEST_F(FakeDdkSysmemPbus, Register) { EXPECT_EQ(ZX_PROTOCOL_SYSMEM, pbus_.registered_proto_id()); }

}  // namespace
}  // namespace sysmem_driver
