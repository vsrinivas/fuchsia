// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/system_instance.h"

#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/process.h>
#include <lib/zx/resource.h>
#include <lib/zx/time.h>

#include <string>

#include <zxtest/zxtest.h>

#include "lib/fidl/llcpp/connect_service.h"
#include "src/devices/bin/driver_manager/coordinator.h"
#include "src/devices/bin/driver_manager/coordinator_test_utils.h"
#include "src/devices/bin/driver_manager/driver_host.h"

namespace fkernel = fuchsia_kernel;

namespace {

// Get the root job from the root job service.
zx_status_t get_root_job(zx::job* root_job) {
  auto client_end = service::Connect<fkernel::RootJob>();
  if (client_end.is_error()) {
    return client_end.error_value();
  }

  auto client = fidl::BindSyncClient(std::move(*client_end));
  auto result = client.Get();
  if (!result.ok()) {
    return result.status();
  }
  *root_job = std::move(result.value().job);
  return ZX_OK;
}

class DummyFsProvider : public FsProvider {
 public:
  ~DummyFsProvider() {}
  fidl::ClientEnd<fuchsia_io::Directory> CloneFs(const char* path) override {
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ZX_ASSERT(endpoints.status_value() == ZX_OK);
    return std::move(endpoints->client);
  }
};

class SystemInstanceTest : public zxtest::Test {
 public:
  SystemInstanceTest()
      : inspect_manager_(loop_.dispatcher()),
        coordinator_(DefaultConfig(nullptr, nullptr, nullptr), &inspect_manager_,
                     loop_.dispatcher()) {}

  SystemInstance under_test_;
  LoaderServiceConnector service_connector_;
  DummyFsProvider fs_provider_;

  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  InspectManager inspect_manager_;
  Coordinator coordinator_;
};

// Verify the job that driver_hosts are launched under also lacks ZX_POL_AMBIENT_MARK_VMO_EXEC.
TEST_F(SystemInstanceTest, DriverHostJobLacksAmbientVmex) {
  zx::job root_job;
  ASSERT_OK(get_root_job(&root_job));

  zx::job driver_job;
  ASSERT_OK(under_test_.CreateDriverHostJob(root_job, &driver_job));

  DriverHostConfig config{
      .name = "ambient_vmex_test_util",
      .binary = "/pkg/bin/ambient_vmex_test_util",
      .env = nullptr,
      .job = zx::unowned(driver_job),
      .root_resource = zx::unowned_resource(),
      .loader_service_connector = &service_connector_,
      .fs_provider = &fs_provider_,
      .coordinator = &coordinator_,
  };
  fbl::RefPtr<DriverHost> host;
  ASSERT_OK(DriverHost::Launch(config, &host));

  ASSERT_OK(host->proc()->wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));
  zx_info_process_t proc_info;
  ASSERT_OK(
      host->proc()->get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr));
  ASSERT_TRUE(proc_info.flags & ZX_INFO_PROCESS_FLAG_EXITED);
  // A return code of 1 from the util process indicates the replace_as_executable call failed with
  // ACCESS_DENIED.
  ASSERT_EQ(proc_info.return_code, 1);
}

TEST_F(SystemInstanceTest, DriverHostJobLacksNewProcess) {
  zx::job root_job;
  ASSERT_OK(get_root_job(&root_job));

  zx::job driver_job;
  ASSERT_OK(under_test_.CreateDriverHostJob(root_job, &driver_job));

  DriverHostConfig config{
      .name = "new_process_test_util",
      .binary = "/pkg/bin/new_process_test_util",
      .env = nullptr,
      .job = zx::unowned(driver_job),
      .root_resource = zx::unowned_resource(),
      .loader_service_connector = &service_connector_,
      .fs_provider = &fs_provider_,
      .coordinator = &coordinator_,
  };
  fbl::RefPtr<DriverHost> host;
  ASSERT_OK(DriverHost::Launch(config, &host));
  ASSERT_OK(host->proc()->wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));
  zx_info_process_t proc_info;
  ASSERT_OK(
      host->proc()->get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr));
  ASSERT_TRUE(proc_info.flags & ZX_INFO_PROCESS_FLAG_EXITED);
  // A return code of 1 from the util process indicates the process_create call failed with
  // ACCESS_DENIED.
  ASSERT_EQ(proc_info.return_code, 1);
}

}  // namespace
