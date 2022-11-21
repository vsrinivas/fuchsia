// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.boot/cpp/fidl.h>
#include <fidl/fuchsia.kernel/cpp/fidl.h>
#include <fidl/fuchsia.scheduler/cpp/fidl.h>
#include <lib/component/incoming/cpp/service_client.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/client.h>
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

TEST(SvchostTest, FuchsiaBootFactoryItemsPresent) {
  auto client_end = component::Connect<fuchsia_boot::FactoryItems>();
  ASSERT_TRUE(client_end.is_ok(), "failed to connect to fuchsia_boot::FactoryItems");

  auto result = fidl::WireCall(*client_end)->Get(0);
  ASSERT_OK(result.status(), "fuchsia_boot::FactoryItems::Get failed");
}

TEST(SvchostTest, FuchsiaBootItemsPresent) {
  auto client_end = component::Connect<fuchsia_boot::Items>();
  ASSERT_TRUE(client_end.is_ok(), "failed to connect to fuchsia_boot::Items");

  auto result = fidl::WireCall(*client_end)->Get(0, 0);
  ASSERT_OK(result.status(), "fuchsia_boot::Items::Get failed");
}

TEST(SvchostTest, FuchsiaReadOnlyBootLogPresent) {
  auto client_end = component::Connect<fuchsia_boot::ReadOnlyLog>();
  ASSERT_TRUE(client_end.is_ok(), "failed to connect to fuchsia_boot::ReadOnlyLog");

  auto result = fidl::WireCall(*client_end)->Get();
  ASSERT_OK(result.status(), "fuchsia_boot::ReadOnlyLog::Get failed");
}

TEST(SvchostTest, FuchsiaWriteOnlyBootLogPresent) {
  auto client_end = component::Connect<fuchsia_boot::WriteOnlyLog>();
  ASSERT_TRUE(client_end.is_ok(), "failed to connect to fuchsia_boot::WriteOnlyLog");

  auto result = fidl::WireCall(*client_end)->Get();
  ASSERT_OK(result.status(), "fuchsia_boot::WriteOnlyLog::Get failed");
}

TEST(SvchostTest, FuchsiaSchedulerProfileProviderPresent) {
  auto client_end = component::Connect<fuchsia_scheduler::ProfileProvider>();
  ASSERT_TRUE(client_end.is_ok(), "failed to connect to fuchsia_scheduler::ProfileProvider");

  auto result = fidl::WireCall(*client_end)->GetProfile(0, "");
  ASSERT_OK(result.status(), "fuchsia_scheduler::ProfileProvider::GetProfile failed");
}

TEST(SvchostTest, FuchsiaRootResourcePresent) {
  auto client_end = component::Connect<fuchsia_boot::RootResource>();
  ASSERT_TRUE(client_end.is_ok(), "failed to connect to fuchsia_boot::RootResource");

  auto result = fidl::WireCall(*client_end)->Get();
  ASSERT_OK(result.status(), "fuchsia_boot::RootResource::Get failed");
}

TEST(SvchostTest, FuchsiaRootJobPresent) {
  auto client_end = component::Connect<fuchsia_kernel::RootJob>();
  ASSERT_TRUE(client_end.is_ok(), "failed to connect to fuchsia_kernel::RootJob");

  auto result = fidl::WireCall(*client_end)->Get();
  ASSERT_OK(result.status(), "fuchsia_kernel::RootJob::Get failed");
}

TEST(SvchostTest, FuchsiaRootJobForInspectPresent) {
  auto client_end = component::Connect<fuchsia_kernel::RootJobForInspect>();
  ASSERT_TRUE(client_end.is_ok(), "failed to connect to fuchsia_kernel::RootJobForInspect");

  auto result = fidl::WireCall(*client_end)->Get();
  ASSERT_OK(result.status(), "fuchsia_kernel::RootJobForInspect::Get failed");

  zx::job job = std::move(result->job);
  ASSERT_TRUE(job.is_valid());
  zx_info_handle_basic_t info;
  zx_status_t status =
      job.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(zx_info_handle_basic_t), nullptr, nullptr);
  ASSERT_OK(status, "zx_object_get_info failed");
  ASSERT_EQ(ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_INSPECT | ZX_RIGHT_ENUMERATE |
                ZX_RIGHT_GET_PROPERTY,
            info.rights);
}

TEST(SvchostTest, FuchsiaKernelStatsPresent) {
  auto client_end = component::Connect<fuchsia_kernel::Stats>();
  ASSERT_TRUE(client_end.is_ok(), "failed to connect to fuchsia_kernel::Stats");

  auto mem_result = fidl::WireCall(*client_end)->GetMemoryStats();
  ASSERT_OK(mem_result.status(), "GetMemoryStats failed");

  auto mem_stats = mem_result.Unwrap();
  ASSERT_GT(mem_stats->stats.total_bytes(), 0);

  auto cpu_result = fidl::WireCall(*client_end)->GetCpuStats();
  ASSERT_OK(cpu_result.status(), "GetCpuStats failed");

  auto cpu_stats = cpu_result.Unwrap();
  ASSERT_GT(cpu_stats->stats.actual_num_cpus, 0);
  ASSERT_EQ(cpu_stats->stats.actual_num_cpus, cpu_stats->stats.per_cpu_stats.count());
}

}  // namespace
