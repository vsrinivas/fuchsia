// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/kernel/c/fidl.h>
#include <fuchsia/kernel/llcpp/fidl.h>
#include <fuchsia/scheduler/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/profile.h>
#include <lib/zx/vmo.h>

#include <zxtest/zxtest.h>

#include "zircon/rights.h"

namespace {

// These are integration tests of svchost which check whether certain services are present in
// the /svc directory exposed by svchost. To verify that the services are actually present we need
// to minimally test that they work, since fdio_service_connect succeeding does not actually mean
// the remote end exists (i.e. you won't observe a PEER_CLOSED error until actually trying to use
// the channel).

constexpr char kFactoryItemsPath[] = "/svc/" fuchsia_boot_FactoryItems_Name;
constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;
constexpr char kProfileProviderPath[] = "/svc/" fuchsia_scheduler_ProfileProvider_Name;
constexpr char kReadOnlyLogPath[] = "/svc/" fuchsia_boot_ReadOnlyLog_Name;
constexpr char kRootJobPath[] = "/svc/" fuchsia_kernel_RootJob_Name;
constexpr char kRootJobForInspectPath[] = "/svc/" fuchsia_kernel_RootJobForInspect_Name;
constexpr char kRootResourcePath[] = "/svc/" fuchsia_boot_RootResource_Name;
constexpr char kWriteOnlyLogPath[] = "/svc/" fuchsia_boot_WriteOnlyLog_Name;

TEST(SvchostTest, FuchsiaBootFactoryItemsPresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kFactoryItemsPath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx::vmo payload;
  uint32_t length;
  status = fuchsia_boot_FactoryItemsGet(client.get(), 0, payload.reset_and_get_address(), &length);
  ASSERT_EQ(ZX_OK, status, "fuchsia_boot_FactoryItemsGet failed");
}

TEST(SvchostTest, FuchsiaBootItemsPresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kItemsPath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx::vmo payload;
  uint32_t length;
  status = fuchsia_boot_ItemsGet(client.get(), 0, 0, payload.reset_and_get_address(), &length);
  ASSERT_EQ(ZX_OK, status, "fuchsia_boot_ItemsGet failed");
}

TEST(SvchostTest, FuchsiaReadOnlyBootLogPresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kReadOnlyLogPath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx::debuglog log;
  status = fuchsia_boot_ReadOnlyLogGet(client.get(), log.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status, "fuchsia_boot_ReadOnlyLogGet failed");
}

TEST(SvchostTest, FuchsiaWriteOnlyBootLogPresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kWriteOnlyLogPath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx::debuglog log;
  status = fuchsia_boot_WriteOnlyLogGet(client.get(), log.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status, "fuchsia_boot_WriteOnlyLogGet failed");
}

TEST(SvchostTest, FuchsiaSchedulerProfileProviderPresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kProfileProviderPath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx_status_t fidl_status = ZX_ERR_INTERNAL;
  zx::profile profile;
  status = fuchsia_scheduler_ProfileProviderGetProfile(client.get(), 0, "", 0, &fidl_status,
                                                       profile.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status, "fuchsia_scheduler_ProfileProviderGetProfile failed");
}

TEST(SvchostTest, FuchsiaRootResourcePresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kRootResourcePath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx::resource resource;
  status = fuchsia_boot_RootResourceGet(client.get(), resource.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status, "fuchsia_boot_RootResourceGet failed");
}

TEST(SvchostTest, FuchsiaRootJobPresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kRootJobPath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx::job job;
  status = fuchsia_kernel_RootJobGet(client.get(), job.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status, "fuchsia_kernel_RootJobGet failed");
}

TEST(SvchostTest, FuchsiaRootJobForInspectPresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  status = fdio_service_connect(kRootJobForInspectPath, server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  zx::job job;
  status = fuchsia_kernel_RootJobForInspectGet(client.get(), job.reset_and_get_address());
  ASSERT_EQ(ZX_OK, status, "fuchsia_kernel_RootJobForInspectGet failed");
  ASSERT_TRUE(job.is_valid());
  zx_info_handle_basic_t info;
  status =
      job.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(zx_info_handle_basic_t), nullptr, nullptr);
  ASSERT_EQ(ZX_OK, status, "zx_object_get_info failed");
  ASSERT_EQ(ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_INSPECT | ZX_RIGHT_ENUMERATE |
                ZX_RIGHT_GET_PROPERTY,
            info.rights);
}

TEST(SvchostTest, FuchsiaKernelStatsPresent) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

  fbl::String service_path = fbl::String::Concat({"/svc/", llcpp::fuchsia::kernel::Stats::Name});
  status = fdio_service_connect(service_path.c_str(), server.release());
  ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

  llcpp::fuchsia::kernel::Stats::SyncClient stats_client(std::move(client));
  llcpp::fuchsia::kernel::Stats::ResultOf::GetMemoryStats mem_result =
      stats_client.GetMemoryStats();
  ASSERT_EQ(ZX_OK, mem_result.status(), "GetMemoryStats failed");

  auto mem_stats = mem_result.Unwrap();
  ASSERT_GT(mem_stats->stats.total_bytes(), 0);

  llcpp::fuchsia::kernel::Stats::ResultOf::GetCpuStats cpu_result = stats_client.GetCpuStats();
  ASSERT_EQ(ZX_OK, cpu_result.status(), "GetCpuStats failed");

  auto cpu_stats = cpu_result.Unwrap();
  ASSERT_GT(cpu_stats->stats.actual_num_cpus, 0);
  ASSERT_EQ(cpu_stats->stats.actual_num_cpus, cpu_stats->stats.per_cpu_stats.count());
}

}  // namespace
