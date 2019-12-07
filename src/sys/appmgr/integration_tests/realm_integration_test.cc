// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/rights.h>

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <fidl/examples/echo/cpp/fidl.h>
#include <fs/pseudo_dir.h>
#include <fs/synchronous_vfs.h>
#include <test/appmgr/integration/cpp/fidl.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/files/glob.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/logging.h"
#include "src/sys/appmgr/component_controller_impl.h"
#include "src/sys/appmgr/integration_tests/util/data_file_reader_writer_util.h"
#include "src/sys/appmgr/realm.h"
#include "src/sys/appmgr/util.h"

namespace component {
namespace {

using fuchsia::sys::TerminationReason;

using ::testing::AnyOf;
using ::testing::Eq;

using sys::testing::EnclosingEnvironment;
using sys::testing::TestWithEnvironment;
using test::appmgr::integration::DataFileReaderWriterPtr;

class RealmTest : virtual public TestWithEnvironment {
 protected:
  void SetUp() override {
    TestWithEnvironment::SetUp();
    OpenNewOutFile();
  }

  void OpenNewOutFile() {
    ASSERT_TRUE(tmp_dir_.NewTempFile(&out_file_));
    outf_ = fileno(std::fopen(out_file_.c_str(), "w"));
  }

  std::string ReadOutFile() {
    std::string out;
    if (!files::ReadFileToString(out_file_, &out)) {
      FXL_LOG(ERROR) << "Could not read output file " << out_file_;
      return "";
    }
    return out;
  }

  fuchsia::sys::LaunchInfo CreateLaunchInfo(const std::string& url,
                                            zx::channel directory_request = zx::channel(),
                                            const std::vector<std::string>& args = {}) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    launch_info.arguments = args;
    if (directory_request.is_valid()) {
      launch_info.directory_request = std::move(directory_request);
    }
    launch_info.out = sys::CloneFileDescriptor(outf_);
    launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
    return launch_info;
  }

  fuchsia::sys::ComponentControllerPtr RunComponent(EnclosingEnvironment* enclosing_environment,
                                                    const std::string& url,
                                                    zx::channel directory_request = zx::channel(),
                                                    const std::vector<std::string>& args = {}) {
    return enclosing_environment->CreateComponent(
        CreateLaunchInfo(url, std::move(directory_request), std::move(args)));
  }

 private:
  files::ScopedTempDir tmp_dir_;
  std::string out_file_;
  int outf_;
};

constexpr char kRealm[] = "realmintegrationtest";

TEST_F(RealmTest, Resolve) {
  auto enclosing_environment = CreateNewEnclosingEnvironment(kRealm, CreateServices());

  auto resolver = enclosing_environment->ConnectToService<fuchsia::process::Resolver>();

  bool wait = false;
  resolver->Resolve(
      "fuchsia-pkg://fuchsia.com/appmgr_integration_tests#test/"
      "appmgr_realm_integration_tests",
      [&wait](zx_status_t status, zx::vmo binary,
              fidl::InterfaceHandle<fuchsia::ldsvc::Loader> loader) {
        wait = true;

        ASSERT_EQ(ZX_OK, status);

        std::string expect;
        files::ReadFileToString("/pkg/test/appmgr_realm_integration_tests", &expect);
        ASSERT_FALSE(expect.empty());

        std::vector<char> buf(expect.length());
        ASSERT_EQ(ZX_OK, binary.read(buf.data(), 0, buf.size()));
        std::string actual(buf.begin(), buf.end());

        ASSERT_EQ(expect, actual);
      });
  RunLoopUntil([&wait] { return wait; });
}

TEST_F(RealmTest, LaunchNonExistentComponent) {
  auto env_services = CreateServices();
  auto enclosing_environment = CreateNewEnclosingEnvironment(kRealm, std::move(env_services));
  WaitForEnclosingEnvToStart(enclosing_environment.get());

  // try to launch file url.
  auto controller1 = RunComponent(enclosing_environment.get(), "does_not_exist");
  bool wait = false;
  controller1.events().OnTerminated = [&wait](int64_t return_code,
                                              fuchsia::sys::TerminationReason reason) {
    wait = true;
    EXPECT_EQ(reason, fuchsia::sys::TerminationReason::PACKAGE_NOT_FOUND);
  };
  RunLoopUntil([&wait] { return wait; });

  // try to launch pkg url.
  auto controller2 = RunComponent(enclosing_environment.get(),
                                  "fuchsia-pkg://fuchsia.com/does_not_exist#meta/some.cmx");
  wait = false;
  controller2.events().OnTerminated = [&wait](int64_t return_code,
                                              fuchsia::sys::TerminationReason reason) {
    wait = true;
    EXPECT_EQ(reason, fuchsia::sys::TerminationReason::PACKAGE_NOT_FOUND);
  };
  RunLoopUntil([&wait] { return wait; });
}

// This test exercises the fact that two components should be in separate jobs,
// and thus when one component controller kills its job due to a .Kill() call
// the other component should run uninterrupted.
TEST_F(RealmTest, CreateTwoKillOne) {
  // launch component as a service.
  auto env_services = CreateServices();
  ASSERT_EQ(ZX_OK, env_services->AddServiceWithLaunchInfo(
                       CreateLaunchInfo("fuchsia-pkg://fuchsia.com/"
                                        "echo_server_cpp#meta/echo_server_cpp.cmx"),
                       fidl::examples::echo::Echo::Name_));
  auto enclosing_environment = CreateNewEnclosingEnvironment(kRealm, std::move(env_services));
  WaitForEnclosingEnvToStart(enclosing_environment.get());
  // launch component normally
  auto controller1 =
      RunComponent(enclosing_environment.get(),
                   "fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx");

  // make sure echo service is running.
  fidl::examples::echo::EchoPtr echo;
  enclosing_environment->ConnectToService(echo.NewRequest());
  const std::string message = "CreateTwoKillOne";
  fidl::StringPtr ret_msg;
  echo->EchoString(message, [&](::fidl::StringPtr retval) { ret_msg = retval; });
  RunLoopUntil([&] { return ret_msg.has_value(); });

  // Kill one of the two components, make sure it's exited via Wait
  bool wait = false;
  controller1.events().OnTerminated =
      [&wait](int64_t return_code, fuchsia::sys::TerminationReason reason) { wait = true; };
  controller1->Kill();
  RunLoopUntil([&wait] { return wait; });

  // Make sure the second component is still running.
  ret_msg.reset();
  echo->EchoString(message, [&](::fidl::StringPtr retval) { ret_msg = retval; });
  RunLoopUntil([&] { return ret_msg.has_value(); });
}

TEST_F(RealmTest, KillRealmKillsComponent) {
  auto env_services = CreateServices();
  ASSERT_EQ(ZX_OK, env_services->AddServiceWithLaunchInfo(
                       CreateLaunchInfo("fuchsia-pkg://fuchsia.com/"
                                        "echo_server_cpp#meta/echo_server_cpp.cmx"),
                       fidl::examples::echo::Echo::Name_));
  auto enclosing_environment = CreateNewEnclosingEnvironment(kRealm, std::move(env_services));
  WaitForEnclosingEnvToStart(enclosing_environment.get());

  // make sure echo service is running.
  fidl::examples::echo::EchoPtr echo;
  enclosing_environment->ConnectToService(echo.NewRequest());
  const std::string message = "CreateTwoKillOne";
  fidl::StringPtr ret_msg;
  echo->EchoString(message, [&](::fidl::StringPtr retval) { ret_msg = retval; });
  RunLoopUntil([&] { return ret_msg.has_value(); });

  bool killed = false;
  echo.set_error_handler([&](zx_status_t status) { killed = true; });
  enclosing_environment->Kill();
  RunLoopUntil([&] { return enclosing_environment->is_running(); });
  // send a msg, without that error handler won't be called.
  echo->EchoString(message, [&](::fidl::StringPtr retval) { ret_msg = retval; });
  RunLoopUntil([&] { return killed; });
}

TEST_F(RealmTest, EnvironmentControllerRequired) {
  fuchsia::sys::EnvironmentPtr env;
  real_env()->CreateNestedEnvironment(env.NewRequest(), /* controller = */ nullptr, kRealm,
                                      /* additional_services = */ nullptr,
                                      fuchsia::sys::EnvironmentOptions{});

  zx_status_t env_status = ZX_OK;
  env.set_error_handler([&](zx_status_t status) { env_status = status; });

  RunLoopUntil([&] { return env_status != ZX_OK; });
}

TEST_F(RealmTest, EnvironmentLabelMustBeUnique) {
  // Create first environment with label kRealm using EnclosingEnvironment since
  // that's easy.
  auto enclosing_environment = CreateNewEnclosingEnvironment(kRealm, CreateServices());

  // Can't use EnclosingEnvironment here since there's no way to discern between
  // 'not yet created' and 'failed to create'. This also lets us check the
  // specific status returned.
  fuchsia::sys::EnvironmentPtr env;
  fuchsia::sys::EnvironmentControllerPtr env_controller;

  zx_status_t env_status, env_controller_status;
  env.set_error_handler([&](zx_status_t status) { env_status = status; });
  env_controller.set_error_handler([&](zx_status_t status) { env_controller_status = status; });

  // Same environment label as EnclosingEnvironment created above.
  real_env()->CreateNestedEnvironment(env.NewRequest(), env_controller.NewRequest(), kRealm,
                                      nullptr, fuchsia::sys::EnvironmentOptions{});

  RunLoopUntil([&] { return env_status == ZX_ERR_BAD_STATE; });
  RunLoopUntil([&] { return env_controller_status == ZX_ERR_BAD_STATE; });
}

TEST_F(RealmTest, RealmJobProvider) {
  fuchsia::sys::JobProviderSyncPtr ptr;
  fdio_service_connect("/hub/job", ptr.NewRequest().TakeChannel().release());

  zx::job job;
  ASSERT_EQ(ZX_OK, ptr->GetJob(&job));

  // check that we can get properties.
  char name[ZX_MAX_NAME_LEN];
  ASSERT_EQ(ZX_OK, job.get_property(ZX_PROP_NAME, name, sizeof(name)));

  EXPECT_THAT(name, ::testing::StartsWith("env_for_test"));
  zx_koid_t koid;
  size_t actual;
  size_t avail_count;
  ASSERT_EQ(ZX_OK,
            job.get_info(ZX_INFO_JOB_CHILDREN, &koid, sizeof(zx_koid_t), &actual, &avail_count));
  ASSERT_EQ(1u, actual);

  // check that we can get children
  zx::job child_job;
  ASSERT_EQ(ZX_OK, job.get_child(koid, ZX_RIGHT_SAME_RIGHTS, &child_job));
}

TEST_F(RealmTest, RealmDiesWhenItsJobDies) {
  auto env_services = CreateServices();

  ASSERT_EQ(ZX_OK, env_services->AddServiceWithLaunchInfo(
                       CreateLaunchInfo("fuchsia-pkg://fuchsia.com/"
                                        "echo_server_cpp#meta/echo_server_cpp.cmx"),
                       fidl::examples::echo::Echo::Name_));
  auto enclosing_environment = CreateNewEnclosingEnvironment(kRealm, std::move(env_services));
  WaitForEnclosingEnvToStart(enclosing_environment.get());

  // make sure echo service is running.
  fidl::examples::echo::EchoPtr echo;
  enclosing_environment->ConnectToService(echo.NewRequest());
  const std::string message = "some_msg";
  fidl::StringPtr ret_msg;
  echo->EchoString(message, [&](::fidl::StringPtr retval) { ret_msg = retval; });
  RunLoopUntil([&] { return ret_msg.has_value(); });

  fuchsia::sys::JobProviderSyncPtr ptr;
  files::Glob glob(std::string("/hub/r/") + kRealm + "/*/job");
  ASSERT_EQ(1u, glob.size());
  const std::string path = *glob.begin();
  fdio_service_connect(path.c_str(), ptr.NewRequest().TakeChannel().release());

  zx::job job;
  EXPECT_EQ(ZX_OK, ptr->GetJob(&job));
  ASSERT_EQ(ZX_OK, job.kill());

  RunLoopUntil([&] { return !enclosing_environment->is_running(); });
}

TEST_F(RealmTest, EmptyRealmDiesWhenItsJobDies) {
  auto env_services = CreateServices();

  auto enclosing_environment = CreateNewEnclosingEnvironment(kRealm, std::move(env_services));
  WaitForEnclosingEnvToStart(enclosing_environment.get());

  fuchsia::sys::JobProviderSyncPtr ptr;
  files::Glob glob(std::string("/hub/r/") + kRealm + "/*/job");
  ASSERT_EQ(1u, glob.size());
  const std::string path = *glob.begin();
  fdio_service_connect(path.c_str(), ptr.NewRequest().TakeChannel().release());

  zx::job job;
  EXPECT_EQ(ZX_OK, ptr->GetJob(&job));
  ASSERT_EQ(ZX_OK, job.kill());

  RunLoopUntil([&] { return !enclosing_environment->is_running(); });
}

TEST_F(RealmTest, KillWorks) {
  auto env_services = CreateServices();

  auto enclosing_environment = CreateNewEnclosingEnvironment(kRealm, std::move(env_services));
  WaitForEnclosingEnvToStart(enclosing_environment.get());

  std::string hub_path = std::string("/hub/r/") + kRealm;
  // make sure realm was created
  files::Glob glob1(hub_path);
  ASSERT_EQ(1u, glob1.size());

  bool killed = false;
  enclosing_environment->Kill([&] { killed = true; });
  RunLoopUntil([&] { return killed; });

  // make sure realm was really killed
  files::Glob glob2(hub_path);
  ASSERT_EQ(0u, glob2.size());
}

class EnvironmentOptionsTest : public RealmTest,
                               public component::testing::DataFileReaderWriterUtil {};

TEST_F(EnvironmentOptionsTest, DeleteStorageOnDeath) {
  constexpr char kTestFileName[] = "some-test-file";
  constexpr char kTestFileContent[] = "the-best-file-content";

  // Create an environment with 'delete_storage_on_death' option enabled.
  zx::channel request;
  auto services = sys::ServiceDirectory::CreateWithRequest(&request);
  DataFileReaderWriterPtr util;
  auto enclosing_environment = CreateNewEnclosingEnvironment(
      kRealm, CreateServices(), fuchsia::sys::EnvironmentOptions{.delete_storage_on_death = true});
  auto ctrl = RunComponent(enclosing_environment.get(),
                           "fuchsia-pkg://fuchsia.com/persistent_storage_test_util#meta/util.cmx",
                           std::move(request));
  services->Connect(util.NewRequest());

  // Write some arbitrary file content into the test util's "/data" dir, and
  // verify that we can read it back.
  ASSERT_EQ(WriteFileSync(util, kTestFileName, kTestFileContent), ZX_OK);
  ASSERT_EQ(ReadFileSync(util, kTestFileName).value_or(""), kTestFileContent);

  // Kill the environment, which should automatically delete any persistent
  // storage it owns.
  bool killed = false;
  enclosing_environment->Kill([&] { killed = true; });
  RunLoopUntil([&] { return killed; });

  // Recreate the environment and component using the same environment label.
  services = sys::ServiceDirectory::CreateWithRequest(&request);
  enclosing_environment = CreateNewEnclosingEnvironment(kRealm, CreateServices());
  ctrl = RunComponent(enclosing_environment.get(),
                      "fuchsia-pkg://fuchsia.com/persistent_storage_test_util#meta/util.cmx",
                      std::move(request));
  services->Connect(util.NewRequest());

  // Verify that the file no longer exists.
  EXPECT_FALSE(ReadFileSync(util, kTestFileName).has_value());
}

using LabelAndValidity = std::tuple<std::string, bool>;
class EnvironmentLabelTest : public RealmTest,
                             public ::testing::WithParamInterface<LabelAndValidity> {};

TEST_P(EnvironmentLabelTest, CheckLabelValidity) {
  // Can't use EnclosingEnvironment here since there's no way to discern between
  // 'not yet created' and 'failed to create'. This also lets use check the
  // specific status returned.
  fuchsia::sys::EnvironmentPtr env;
  fuchsia::sys::EnvironmentControllerPtr env_controller;

  zx_status_t env_status = ZX_OK;
  zx_status_t env_controller_status = ZX_OK;
  bool env_created = false;
  env.set_error_handler([&](zx_status_t status) { env_status = status; });
  env_controller.set_error_handler([&](zx_status_t status) { env_controller_status = status; });
  env_controller.events().OnCreated = [&] { env_created = true; };

  auto [label, label_valid] = GetParam();
  real_env()->CreateNestedEnvironment(env.NewRequest(), env_controller.NewRequest(), label,
                                      /* additional_services = */ nullptr,
                                      fuchsia::sys::EnvironmentOptions{});

  if (label_valid) {
    RunLoopUntil([&] { return env_created; });
  } else {
    RunLoopUntil([&] { return env_status == ZX_ERR_INVALID_ARGS; });
    RunLoopUntil([&] { return env_controller_status == ZX_ERR_INVALID_ARGS; });
    EXPECT_FALSE(env_created);
  }
}

INSTANTIATE_TEST_SUITE_P(
    InvalidLabels, EnvironmentLabelTest,
    ::testing::Combine(::testing::Values("", "a/b", "/", ".", "..", "../..", "\t", "\r", "ab\n",
                                         std::string("123\0", 4), "\10", "\33", "\177", " ",
                                         "my realm", "~", "`", "!", "@", "$", "%", "^", "&", "*",
                                         "(", ")", "=", "+", "{", "}", "[", "]", "|", "?", ";", "'",
                                         "\"", "<", ">", ",",
                                         "fuchsia-pkg://fuchsia.com/abcd#meta/abcd.cmx"),
                       ::testing::Values(false)));

INSTANTIATE_TEST_SUITE_P(
    ValidLabels, EnvironmentLabelTest,
    ::testing::Combine(::testing::Values("abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
                                         "0123456789", "#-_:.", "my.realm", "my..realm",
                                         "fuchsia-pkg:::fuchsia.com:abcd#meta:abcd.cmx"),
                       ::testing::Values(true)));

class RealmFakeLoaderTest : public RealmTest, public fuchsia::sys::Loader {
 protected:
  RealmFakeLoaderTest() {
    sys::testing::EnvironmentServices::ParentOverrides parent_overrides;
    parent_overrides.loader_service_ =
        std::make_shared<vfs::Service>([this](zx::channel channel, async_dispatcher_t* dispatcher) {
          bindings_.AddBinding(this,
                               fidl::InterfaceRequest<fuchsia::sys::Loader>(std::move(channel)));
        });
    enclosing_environment_ = CreateNewEnclosingEnvironment(
        kRealm, CreateServicesWithParentOverrides(std::move(parent_overrides)));
  }

  void LoadUrl(std::string url, LoadUrlCallback callback) override {
    ASSERT_TRUE(component_url_.empty());
    component_url_ = url;
  }

  void WaitForComponentLoad() {
    RunLoopUntil([this] { return !component_url_.empty(); });
  }

  const std::string& component_url() const { return component_url_; }

  std::unique_ptr<EnclosingEnvironment> enclosing_environment_;

 private:
  std::shared_ptr<vfs::Service> loader_service_;
  fidl::BindingSet<fuchsia::sys::Loader> bindings_;
  std::string component_url_;
};

TEST_F(RealmFakeLoaderTest, CreateInvalidComponent) {
  TerminationReason reason = TerminationReason::UNKNOWN;
  int64_t return_code = INT64_MAX;
  auto controller = RunComponent(enclosing_environment_.get(), "garbage://test");
  controller.events().OnTerminated = [&](int64_t err, TerminationReason r) {
    return_code = err;
    reason = r;
  };
  RunLoopUntil([&] { return return_code < INT64_MAX; });
  EXPECT_EQ(TerminationReason::URL_INVALID, reason);
  EXPECT_EQ(-1, return_code);
}

}  // namespace
}  // namespace component
