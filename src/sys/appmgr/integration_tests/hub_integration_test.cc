// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/inspect/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/inspect/service/cpp/reader.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include <fidl/examples/echo/cpp/fidl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "garnet/bin/sysmgr/config.h"
#include "lib/inspect/cpp/hierarchy.h"
#include "lib/inspect/cpp/vmo/types.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/glob.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace component {
namespace {

// This test fixture will provide a way to run components in provided launchers
// and check for errors.
class HubTest : public sys::testing::TestWithEnvironment {
 protected:
  // This would launch component and check that it returns correct
  // |expected_return_code|.
  void RunComponent(const fuchsia::sys::LauncherPtr& launcher, const std::string& component_url,
                    const std::vector<std::string>& args, int64_t expected_return_code) {
    std::FILE* outf = std::tmpfile();
    int out_fd = fileno(outf);
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = component_url;
    launch_info.arguments = args;

    launch_info.out = sys::CloneFileDescriptor(out_fd);

    fuchsia::sys::ComponentControllerPtr controller;
    launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

    int64_t return_code = INT64_MIN;
    controller.events().OnTerminated = [&return_code](int64_t code,
                                                      fuchsia::sys::TerminationReason reason) {
      return_code = code;
    };
    RunLoopUntil([&return_code] { return return_code != INT64_MIN; });
    std::string output;
    ASSERT_TRUE(files::ReadFileDescriptorToString(out_fd, &output));
    EXPECT_EQ(expected_return_code, return_code)
        << "failed for: " << fxl::JoinStrings(args, ", ") << "\noutput: " << output;
  }
};

using ProbeHub = HubTest;

TEST_F(ProbeHub, Component) {
  std::string echo_url = "fuchsia-pkg://fuchsia.com/appmgr_integration_tests#meta/echo_server.cmx";

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = echo_url;
  auto echo_svc = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);

  fuchsia::sys::ComponentControllerPtr controller;
  launcher_ptr()->CreateComponent(std::move(launch_info), controller.NewRequest());

  bool directory_ready = false;
  controller.events().OnDirectoryReady = [&] { directory_ready = true; };

  RunLoopUntil([&]() { return directory_ready; });

  constexpr char kGlob[] = "/hub/c/echo_server.cmx/*/out/debug";
  files::Glob glob(kGlob);
  EXPECT_GE(glob.size(), 1u) << kGlob << " expected to match at least once.";

  std::string kGlob_svc =
      std::string("/hub/c/echo_server.cmx/*/out/svc/") + fidl::examples::echo::Echo::Name_;
  files::Glob glob_svc(kGlob_svc);
  EXPECT_GE(glob_svc.size(), 1u) << kGlob_svc << " expected to match at least once.";
}

TEST_F(ProbeHub, Realm) {
  constexpr char kGlob[] = "/hub/c/";
  files::Glob glob(kGlob);
  EXPECT_EQ(glob.size(), 1u) << kGlob << " expected to match once.";
}

TEST_F(ProbeHub, RealmSvc) {
  constexpr char kGlob[] = "/hub/svc/fuchsia.sys.Environment";
  files::Glob glob(kGlob);
  EXPECT_EQ(glob.size(), 1u);
}

TEST_F(HubTest, Services) {
  // Services for sys.
  {
    constexpr char kGlob[] = "/hub/svc";
    files::Glob glob(kGlob);
    EXPECT_EQ(glob.size(), 1u) << kGlob << " expected to match once.";
    const std::string path = *glob.begin();

    // Expect at least these built-in services
    std::vector<std::string> expected_files = {".",
                                               "fuchsia.boot.FactoryItems",
                                               "fuchsia.boot.ReadOnlyLog",
                                               "fuchsia.boot.RootJob",
                                               "fuchsia.boot.RootJobForInspect",
                                               "fuchsia.boot.RootResource",
                                               "fuchsia.boot.WriteOnlyLog",
                                               "fuchsia.device.NameProvider",
                                               "fuchsia.device.manager.Administrator",
                                               "fuchsia.device.manager.DebugDumper",
                                               "fuchsia.hardware.pty.Device",
                                               "fuchsia.kernel.Counter",
                                               "fuchsia.kernel.DebugBroker",
                                               "fuchsia.kernel.Stats",
                                               "fuchsia.tracing.kernel.Controller",
                                               "fuchsia.tracing.kernel.Reader",
                                               "fuchsia.paver.Paver",
                                               "fuchsia.process.Launcher",
                                               "fuchsia.process.Resolver",
                                               "fuchsia.scheduler.ProfileProvider",
                                               "fuchsia.security.resource.Vmex",
                                               "fuchsia.sys.Environment",
                                               "fuchsia.sys.Launcher",
                                               "fuchsia.sys.Loader",
                                               "fuchsia.sys.test.CacheControl",
                                               "fuchsia.sysinfo.SysInfo",
                                               "fuchsia.virtualconsole.SessionManager"};
    sysmgr::Config config;
    const auto service_map = config.TakeServices();
    for (const auto& e : service_map) {
      expected_files.push_back(e.first);
    }

    // readdir should list all services.
    std::vector<std::string> files;
    ASSERT_TRUE(files::ReadDirContents(path, &files));
    EXPECT_THAT(expected_files, ::testing::IsSubsetOf(files));
  }
}

TEST_F(HubTest, ScopePolicy) {
  constexpr char kGlobUrl[] = "fuchsia-pkg://fuchsia.com/glob#meta/glob.cmx";
  // create nested environment
  // test that we can see nested env
  auto nested_env = CreateNewEnclosingEnvironment("hubscopepolicytest", CreateServices());
  WaitForEnclosingEnvToStart(nested_env.get());
  RunComponent(launcher_ptr(), kGlobUrl, {"/hub/r/hubscopepolicytest/"}, 0);

  // test that we cannot see nested env using its own launcher
  RunComponent(nested_env->launcher_ptr(), kGlobUrl, {"/hub/r/hubscopepolicytest"}, 1);

  // test that we can see check_hub_path
  RunComponent(nested_env->launcher_ptr(), kGlobUrl, {"/hub/c/glob.cmx"}, 0);
}

TEST_F(HubTest, SystemDiagnostics) {
  std::string glob_url = "fuchsia-pkg://fuchsia.com/glob#meta/glob.cmx";

  auto nested_env = CreateNewEnclosingEnvironment("hubscopepolicytest", CreateServices());
  WaitForEnclosingEnvToStart(nested_env.get());
  RunComponent(launcher_ptr(), glob_url, {"/hub/r/hubscopepolicytest/"}, 0);

  // test that we can see system objects
  RunComponent(nested_env->launcher_ptr(), glob_url, {"/hub/c/glob.cmx/*/system_diagnostics"}, 0);
}

TEST_F(HubTest, SystemDiagnosticsData) {
  std::string url =
      "fuchsia-pkg://fuchsia.com/appmgr_integration_tests#meta/"
      "appmgr_integration_tests_inspect_test_app.cmx";

  auto env_name = fxl::StringPrintf("test-%lu", time(NULL));

  auto nested_env = CreateNewEnclosingEnvironment(env_name, CreateServices());
  WaitForEnclosingEnvToStart(nested_env.get());

  fuchsia::sys::ComponentControllerPtr controller = nested_env->CreateComponentFromUrl(url);
  bool ready = false;
  controller.events().OnDirectoryReady = [&] { ready = true; };
  RunLoopUntil([&] { return ready; });

  std::vector<std::string> paths;
  for (auto val : files::Glob(
           fxl::StringPrintf("/hub/r/%s/*/c/appmgr_integration_tests_inspect_test_app.cmx/*/"
                             "system_diagnostics/*",
                             env_name.c_str()))) {
    paths.emplace_back(std::move(val));
  }

  ASSERT_EQ(1U, paths.size());

  fuchsia::inspect::TreePtr tree;
  fdio_service_connect(paths[0].c_str(), tree.NewRequest().TakeChannel().release());
  auto read = inspect::ReadFromTree(std::move(tree));
  async::Executor executor_(dispatcher());

  fit::result<inspect::Hierarchy> result;
  executor_.schedule_task(
      read.then([&](fit::result<inspect::Hierarchy>& res) { result = std::move(res); }));

  RunLoopUntil([&] { return !!result; });

  ASSERT_TRUE(result.is_ok());

  auto* threads = result.value().GetByPath({"threads"});
  ASSERT_NE(nullptr, threads);
  EXPECT_NE(0u, threads->children().size());
  auto* stack = threads->children()[0].GetByPath({"stack"});
  ASSERT_NE(nullptr, stack);
  auto* dump = stack->node().get_property<inspect::StringPropertyValue>("dump");
  ASSERT_NE(nullptr, dump);

  auto* handle_count = result.value().GetByPath({"handle_count"});
  ASSERT_NE(nullptr, handle_count);
  auto* vmo = handle_count->node().get_property<inspect::UintPropertyValue>("vmo");
  ASSERT_NE(nullptr, vmo);
  EXPECT_NE(0u, vmo->value());

  auto* memory = result.value().GetByPath({"memory"});
  ASSERT_NE(nullptr, memory);
  std::vector<std::string> names;
  for (const auto& prop : memory->node().properties()) {
    names.emplace_back(prop.name());
  }

  EXPECT_THAT(names, testing::UnorderedElementsAre("mapped_bytes", "shared_bytes", "private_bytes",
                                                   "scaled_shared_bytes"));
}

TEST_F(HubTest, SystemDiagnosticsInUseWhileFreed) {
  std::string url =
      "fuchsia-pkg://fuchsia.com/appmgr_integration_tests#meta/"
      "appmgr_integration_tests_inspect_test_app.cmx";

  auto env_name = fxl::StringPrintf("test-%lu", time(NULL));

  auto nested_env = CreateNewEnclosingEnvironment(env_name, CreateServices());
  WaitForEnclosingEnvToStart(nested_env.get());

  fuchsia::sys::ComponentControllerPtr controller = nested_env->CreateComponentFromUrl(url);
  bool ready = false;
  controller.events().OnDirectoryReady = [&] { ready = true; };
  RunLoopUntil([&] { return ready; });

  std::vector<std::string> paths;
  for (auto val : files::Glob(
           fxl::StringPrintf("/hub/r/%s/*/c/appmgr_integration_tests_inspect_test_app.cmx/*/"
                             "system_diagnostics/*",
                             env_name.c_str()))) {
    paths.emplace_back(std::move(val));
  }

  ASSERT_EQ(1U, paths.size());

  fuchsia::inspect::TreePtr tree;

  fdio_service_connect(paths[0].c_str(), tree.NewRequest().TakeChannel().release());

  // Get the name of a single child.
  fuchsia::inspect::TreeNameIteratorPtr iterator;
  tree->ListChildNames(iterator.NewRequest());
  std::vector<std::string> child_names;
  iterator->GetNext([&](std::vector<std::string> names) { child_names = std::move(names); });
  RunLoopUntil([&] { return !child_names.empty(); });

  // Open the child.
  fuchsia::inspect::TreePtr child;
  bool error = false;
  child.set_error_handler([&](zx_status_t unused) { error = true; });
  tree->OpenChild(child_names[0], child.NewRequest());

  {
    // Ensure we can get the child's content.
    bool done = false;
    child->GetContent([&](fuchsia::inspect::TreeContent unused) { done = true; });
    RunLoopUntil([&] { return done || error; });
    ASSERT_FALSE(error);
  }

  // Terminate the component while holding a reference to one of the lazy nodes.
  bool terminated = false;
  controller.events().OnTerminated = [&](uint64_t status, fuchsia::sys::TerminationReason reason) {
    terminated = true;
  };
  controller->Kill();
  RunLoopUntil([&] { return terminated; });
  controller.Unbind();

  bool done = false;
  // Try to construct lazy node content. This should not succeed, but the appmgr should not crash.
  child->GetContent([&](fuchsia::inspect::TreeContent unused) { done = true; });
  RunLoopUntil([&] { return done || error; });
  EXPECT_TRUE(error);
  EXPECT_FALSE(done);
}

}  // namespace
}  // namespace component
