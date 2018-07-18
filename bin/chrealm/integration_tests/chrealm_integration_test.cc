// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <glob.h>
#include <unistd.h>
#include <cstdio>
#include <string>
#include <vector>

#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/testing/chrealm/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "garnet/bin/appmgr/util.h"
#include "gtest/gtest.h"
#include "lib/component/cpp/environment_services.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/split_string.h"
#include "lib/gtest/real_loop_fixture.h"
#include "lib/svc/cpp/services.h"

namespace chrealm {
namespace {

const char kMessage[] = "hello";
const char kRealm[] = "chrealmtest";

class ChrealmTest : public ::gtest::RealLoopFixture,
                    public fuchsia::testing::chrealm::TestService {
 public:
  void GetMessage(
      fuchsia::testing::chrealm::TestService::GetMessageCallback cb) override {
    cb(kMessage);
  }

 protected:
  ChrealmTest()
      : vfs_(async_get_default_dispatcher()),
        services_(fbl::AdoptRef(new fs::PseudoDir)) {}

  void SetUp() override {
    component::ConnectToEnvironmentService(sys_env_.NewRequest());
    sys_env_->GetServices(svc_.NewRequest());
    // Add a TestService that the test realm can use.
    zx_status_t status = services_->AddEntry(
        fuchsia::testing::chrealm::TestService::Name_,
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          bindings_.AddBinding(
              this,
              fidl::InterfaceRequest<fuchsia::testing::chrealm::TestService>(
                  std::move(channel)));
          return ZX_OK;
        })));
    ASSERT_EQ(ZX_OK, status);
    // Loader service is needed for 'run'.
    status = services_->AddEntry(
        fuchsia::sys::Loader::Name_,
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          svc_->ConnectToService(fuchsia::sys::Loader::Name_,
                                 std::move(channel));
          return ZX_OK;
        })));
    ASSERT_EQ(ZX_OK, status);
  }

  void TearDown() override {
    KillRealm();
  }

  void CreateRealm(std::string* realm_path) {
    static const char kRealmGlob[] = "/hub/r/sys/*/r/chrealmtest/*";

    // Verify the realm doesn't exist to start with.
    glob_t globbuf;
    ASSERT_EQ(GLOB_NOMATCH, glob(kRealmGlob, 0, nullptr, &globbuf));

    // Create a nested realm to test with.
    fuchsia::sys::EnvironmentPtr nested_env;
    sys_env_->CreateNestedEnvironment(
        component::Util::OpenAsDirectory(&vfs_, services_),
        nested_env.NewRequest(), controller_.NewRequest(), kRealm);
    bool started = false;
    controller_.events().OnCreated = [&started](){ started = true; };
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&started] { return started; },
                                          zx::sec(5)));

    // Get the path to the test realm in /hub. Test is running in the root
    // realm, so we find the realm under sys.
    ASSERT_EQ(0, glob(kRealmGlob, 0, nullptr, &globbuf))
        << "glob found no matches";
    ASSERT_EQ(1u, globbuf.gl_pathc);
    *realm_path = globbuf.gl_pathv[0];
    globfree(&globbuf);
  }

  void KillRealm() {
    if (controller_) {
      bool alive = true;
      controller_.set_error_handler([&alive] { alive = false; });
      controller_->Kill([](){});
      ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
          [&alive] { return !alive; }, zx::sec(5)));
      controller_.Unbind();
    }
  }

  void RunCommand(const std::vector<const char*>& argv, std::string* out) {
    *out = "";

    FILE* tmpf = std::tmpfile();
    if (tmpf == nullptr) {
      FAIL() << "Could not open temp file";
    }
    const int tmpfd = fileno(tmpf);
    zx_handle_t proc = ZX_HANDLE_INVALID;
    RunCommandAsync(argv, tmpfd, &proc);
    bool proc_terminated = false;
    async::Wait async_wait(
        proc, ZX_PROCESS_TERMINATED,
        [&proc_terminated](async_t* async, async::WaitBase* wait,
                           zx_status_t status, const zx_packet_signal* signal) {
          proc_terminated = true;
        });
    ASSERT_EQ(ZX_OK, async_wait.Begin(dispatcher()));
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&proc_terminated] { return proc_terminated; }, zx::sec(10)));
    zx_info_process_t info;
    zx_status_t status = zx_object_get_info(proc, ZX_INFO_PROCESS, &info,
                                            sizeof(info), nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(info.return_code, 0)
        << "Command failed with code " << info.return_code;

    ASSERT_TRUE(files::ReadFileDescriptorToString(tmpfd, out));
    close(tmpfd);
  }

  static void RunCommandAsync(const std::vector<const char*>& argv, int outfd,
                              zx_handle_t* proc) {
    std::vector<const char*> normalized_argv = argv;
    normalized_argv.push_back(nullptr);

    // Redirect process's stdout to file.
    fdio_spawn_action_t actions[] = {
        {.action = FDIO_SPAWN_ACTION_CLONE_FD,
         .fd = {.local_fd = outfd, .target_fd = STDOUT_FILENO}},
        {.action = FDIO_SPAWN_ACTION_CLONE_FD,
         .fd = {.local_fd = STDIN_FILENO, .target_fd = STDIN_FILENO}},
        {.action = FDIO_SPAWN_ACTION_CLONE_FD,
         .fd = {.local_fd = STDERR_FILENO, .target_fd = STDERR_FILENO}}};
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    zx_status_t status = fdio_spawn_etc(
        ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO,
        argv[0], normalized_argv.data(), nullptr, countof(actions), actions,
        proc, err_msg);
    ASSERT_EQ(status, ZX_OK) << "Failed to spawn command: " << err_msg;
  }

 private:
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> services_;
  fuchsia::sys::EnvironmentPtr sys_env_;
  fuchsia::sys::ServiceProviderPtr svc_;
  fuchsia::sys::EnvironmentControllerPtr controller_;
  fidl::BindingSet<fuchsia::testing::chrealm::TestService> bindings_;
};

TEST_F(ChrealmTest, ConnectToService) {
  std::string realm_path;
  CreateRealm(&realm_path);

  // List services under /hub -- expect same as /svc.
  {
    // TODO(geb): Explicitly check for testing.chrealm.TestService once
    // CP-23 is fixed. Currently this trivially passes because readdir
    // only finds the built-in services.
    std::string svc_contents;
    std::string hub_contents;
    const std::vector<const char*> lssvc_args = {"/system/bin/chrealm",
                                                 realm_path.c_str(), "--",
                                                 "/system/bin/ls", "/svc"};
    const std::vector<const char*> lshub_args = {"/system/bin/chrealm",
                                                 realm_path.c_str(), "--",
                                                 "/system/bin/ls", "/hub/svc"};
    RunCommand(lssvc_args, &svc_contents);
    RunCommand(lshub_args, &hub_contents);
    EXPECT_EQ(svc_contents, hub_contents);
  }

  // Run chrealm under the test realm with the getmessage program, which
  // should attempt to reach TestService.
  {
    std::string out;
    const std::vector<const char*> args = {
        "/system/bin/chrealm", realm_path.c_str(), "--", "/system/bin/run",
        "chrealm_test_get_message"};
    RunCommand(args, &out);
    EXPECT_EQ(kMessage, out);
  }
}

TEST_F(ChrealmTest, CreatedUnderRealmJob) {
  std::string realm_path;
  CreateRealm(&realm_path);

  zx_handle_t proc = ZX_HANDLE_INVALID;
  const std::vector<const char*> args = {
      "/system/bin/chrealm", realm_path.c_str(), "--", "/system/bin/yes"};
  int pipefd[2];
  ASSERT_EQ(0, pipe(pipefd));
  RunCommandAsync(args, pipefd[1], &proc);
  // Command should run, look for "y".
  char buf[1];
  ASSERT_EQ(1u, read(pipefd[0], buf, 1));
  ASSERT_EQ("y", std::string(buf, 1));

  // Now kill the realm. Wait should complete because the realm's job was
  // killed.
  KillRealm();
  zx_status_t status = zx_object_wait_one(
      proc, ZX_PROCESS_TERMINATED, zx_deadline_after(ZX_SEC(10)), nullptr);
  ASSERT_EQ(ZX_OK, status);
}

}  // namespace
}  // namespace chrealm
