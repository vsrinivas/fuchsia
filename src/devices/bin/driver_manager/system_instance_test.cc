// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system_instance.h"

#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/kernel/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>

#include <map>
#include <string>

#include <zxtest/zxtest.h>

namespace fboot = fuchsia_boot;
namespace fkernel = fuchsia_kernel;

namespace {

// Get the root job from the root job service.
zx_status_t get_root_job(zx::job* root_job) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_service_connect("/svc/fuchsia.kernel.RootJob", remote.release());
  if (status != ZX_OK) {
    return status;
  }

  fidl::WireSyncClient<fkernel::RootJob> client{std::move(local)};
  auto result = client.Get();
  if (!result.ok()) {
    return result.status();
  }
  *root_job = std::move(result.value().job);
  return ZX_OK;
}

class FakeBootArgsServer final : public fidl::WireInterface<fboot::Arguments> {
 public:
  FakeBootArgsServer() : values_() {}

  void SetBool(std::string key, bool value) { values_.insert_or_assign(key, value); }

  // fidl::WireInterface<fuchsia_boot::Arguments> methods:
  void GetString(::fidl::StringView key, GetStringCompleter::Sync& completer) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetStrings(::fidl::VectorView<::fidl::StringView> keys,
                  GetStringsCompleter::Sync& completer) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetBool(::fidl::StringView key, bool defaultval, GetBoolCompleter::Sync& completer) {
    bool result = defaultval;
    auto value = values_.find(std::string(key.data()));
    if (value != values_.end()) {
      result = value->second;
    }
    completer.Reply(result);
  }

  void GetBools(::fidl::VectorView<fboot::wire::BoolPair> keys,
                GetBoolsCompleter::Sync& completer) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Collect(::fidl::StringView prefix, CollectCompleter::Sync& completer) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

 private:
  std::map<std::string, bool> values_;
};

class SystemInstanceForTest : public SystemInstance {
 public:
  using SystemInstance::launcher;
};

class SystemInstanceTest : public zxtest::Test {
 public:
  SystemInstanceTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    ASSERT_OK(loop_.StartThread());

    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    boot_args_server_.reset(new FakeBootArgsServer());
    fidl::BindSingleInFlightOnly(loop_.dispatcher(), std::move(server), boot_args_server_.get());
    boot_args_client_ = fidl::WireSyncClient<fboot::Arguments>(std::move(client));

    under_test_.reset(new SystemInstanceForTest());
  }

  std::unique_ptr<FakeBootArgsServer> boot_args_server_;
  fidl::WireSyncClient<fboot::Arguments> boot_args_client_;
  std::unique_ptr<SystemInstanceForTest> under_test_;

 private:
  async::Loop loop_;
};

// Verify the job that driver_hosts are launched under also lacks ZX_POL_AMBIENT_MARK_VMO_EXEC.
TEST_F(SystemInstanceTest, DriverHostJobLacksAmbientVmex) {
  zx::job root_job;
  ASSERT_OK(get_root_job(&root_job));

  zx::job driver_job;
  ASSERT_OK(under_test_->CreateDriverHostJob(root_job, &driver_job));

  zx::process proc;
  const char* args[] = {"/pkg/bin/ambient_vmex_test_util", nullptr};
  ASSERT_OK(under_test_->launcher().Launch(driver_job, args[0], args, nullptr, -1, zx::resource(),
                                           nullptr, nullptr, 0, &proc, 0));

  ASSERT_OK(proc.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));
  zx_info_process_t proc_info;
  ASSERT_OK(proc.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr));
  ASSERT_TRUE(proc_info.flags & ZX_INFO_PROCESS_FLAG_EXITED);
  // A return code of 1 from the util process indicates the replace_as_executable call failed with
  // ACCESS_DENIED.
  ASSERT_EQ(proc_info.return_code, 1);
}

TEST_F(SystemInstanceTest, DriverHostJobLacksNewProcess) {
  zx::job root_job;
  ASSERT_OK(get_root_job(&root_job));

  zx::job driver_job;
  ASSERT_OK(under_test_->CreateDriverHostJob(root_job, &driver_job));

  zx::process proc;
  const char* args[] = {"/pkg/bin/new_process_test_util", nullptr};
  ASSERT_OK(under_test_->launcher().Launch(driver_job, args[0], args, nullptr, -1, zx::resource(),
                                           nullptr, nullptr, 0, &proc, 0));

  ASSERT_OK(proc.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));
  zx_info_process_t proc_info;
  ASSERT_OK(proc.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr));
  ASSERT_TRUE(proc_info.flags & ZX_INFO_PROCESS_FLAG_EXITED);
  // A return code of 1 from the util process indicates the process_create call failed with
  // ACCESS_DENIED.
  ASSERT_EQ(proc_info.return_code, 1);
}

}  // namespace
