// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/debugdata/cpp/fidl.h>
#include <fuchsia/process/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <sys/stat.h>
#include <zircon/status.h>

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/files/file.h"

TEST(Run, TestHermeticEnv) {
  std::string hub_name;
  files::ReadFileToString("/hub/name", &hub_name);
  // if this was not executed as component, /hub/name would be sys
  EXPECT_THAT(hub_name, testing::MatchesRegex("^test_env_[0-9a-f]{8}$"));
}

class FakeDebugData : public fuchsia::debugdata::DebugData {
 public:
  void Publish(std::string /*unused*/, ::zx::vmo /*unused*/) override { call_count_++; }

  void LoadConfig(std::string /*unused*/, LoadConfigCallback /*unused*/) override {
    call_count_++;
    // not implemented
  }

  fidl::InterfaceRequestHandler<fuchsia::debugdata::DebugData> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return bindings_.GetHandler(this, dispatcher);
  }

  uint64_t call_count() const { return call_count_; }

 private:
  fidl::BindingSet<fuchsia::debugdata::DebugData> bindings_;
  uint64_t call_count_ = 0;
};

using RunFixture = gtest::RealLoopFixture;

TEST_F(RunFixture, ExposesDebugDataService) {
  auto env_services = sys::ServiceDirectory::CreateFromNamespace();

  // It is not possible to use the /bin trampoline unless
  // fuchsia.process.Resolver is proxied to the child process.
  const char* run_d_command_argv[] = {
      "/bin/run-test-component",
      "fuchsia-pkg://fuchsia.com/run_test_component_test#meta/coverage_component.cmx", nullptr};

  zx::job job;
  uint32_t flags = FDIO_SPAWN_DEFAULT_LDSVC | (FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_NAMESPACE);

  sys::testing::ServiceDirectoryProvider service_provider(dispatcher());
  FakeDebugData debugdata;
  service_provider.AddService(debugdata.GetHandler(dispatcher()));

  auto allow_parent_service = [&service_provider,
                               env_services = env_services](const std::string& service_name) {
    service_provider.AddService(
        std::make_unique<vfs::Service>([env_services, service_name = service_name](
                                           zx::channel channel, async_dispatcher_t* /*unused*/) {
          env_services->Connect(service_name, std::move(channel));
        }),
        service_name);
  };

  // Add services required by run-test-component
  allow_parent_service(fuchsia::sys::Environment::Name_);
  allow_parent_service(fuchsia::process::Resolver::Name_);
  allow_parent_service(fuchsia::sys::Loader::Name_);

  std::vector<fdio_spawn_action_t> fdio_actions = {
      fdio_spawn_action_t{.action = FDIO_SPAWN_ACTION_SET_NAME,
                          .name = {.data = "run-test-component"}},
  };

  auto action_ns_entry = [](const char* prefix, zx_handle_t handle) {
    return fdio_spawn_action{.action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
                             .ns = {
                                 .prefix = prefix,
                                 .handle = handle,
                             }};
  };

  // Export the root namespace.
  fdio_flat_namespace_t* flat;
  auto status = fdio_ns_export_root(&flat);
  ASSERT_EQ(ZX_OK, status) << "FAILURE: Cannot export root namespace:"
                           << zx_status_get_string(status);

  for (size_t i = 0; i < flat->count; ++i) {
    if (!strcmp(flat->path[i], "/svc")) {
      // ...and replace it with the proxy /svc.
      fdio_actions.push_back(action_ns_entry(
          "/svc", service_provider.service_directory()->CloneChannel().TakeChannel().release()));
    } else {
      fdio_actions.push_back(action_ns_entry(flat->path[i], flat->handle[i]));
    }
  }

  zx::process run_process;

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];

  ASSERT_EQ(ZX_OK, fdio_spawn_etc(job.get(), flags,
                                  run_d_command_argv[0],  // path
                                  run_d_command_argv,
                                  nullptr,  // environ
                                  fdio_actions.size(), fdio_actions.data(),
                                  run_process.reset_and_get_address(), err_msg))
      << err_msg;

  RunLoopUntil([&]() { return debugdata.call_count() >= 1u; });
}
