// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/testing/chrealm/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/files/glob.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/sys/appmgr/util.h"

namespace chrealm {
namespace {

const char kMessage[] = "hello";
const char kRealm[] = "chrealmtest";

class ChrealmTest : public sys::testing::TestWithEnvironment,
                    public fuchsia::testing::chrealm::TestService {
 public:
  void GetMessage(fuchsia::testing::chrealm::TestService::GetMessageCallback cb) override {
    cb(kMessage);
  }

 protected:
  void CreateRealm(std::string* realm_path) {
    const std::string kRealmGlob = fxl::StringPrintf("/hub/r/%s/*", kRealm);

    ASSERT_EQ(files::Glob(kRealmGlob).size(), 0u);

    // Add a TestService that the test realm can use.
    auto services = CreateServices();
    ASSERT_EQ(ZX_OK, services->AddService(bindings_.GetHandler(this)));
    // Create a nested realm to test with.
    enclosing_env_ = CreateNewEnclosingEnvironment(kRealm, std::move(services));
    WaitForEnclosingEnvToStart(enclosing_env_.get());

    // Get the path to the test realm in /hub. Test is running in the root
    // realm, so we find the realm under sys.
    files::Glob glob(kRealmGlob);
    ASSERT_EQ(glob.size(), 1u);
    *realm_path = *glob.begin();
  }

  void KillRealm() {
    enclosing_env_->Kill();
    ASSERT_TRUE(
        RunLoopWithTimeoutOrUntil([this] { return !enclosing_env_->is_running(); }, zx::sec(10)));
  }

  void RunCommand(const std::vector<const char*>& argv, std::string* out) {
    *out = "";
    FILE* outf = std::tmpfile();
    if (outf == nullptr) {
      FAIL() << "Could not open temp file";
    }
    const int outfd = fileno(outf);
    zx_handle_t proc = ZX_HANDLE_INVALID;
    RunCommandAsync(argv, outfd, &proc);
    bool proc_terminated = false;
    async::Wait async_wait(proc, ZX_PROCESS_TERMINATED, 0,
                           [&proc_terminated](async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                              zx_status_t status, const zx_packet_signal* signal) {
                             proc_terminated = true;
                           });
    ASSERT_EQ(ZX_OK, async_wait.Begin(dispatcher()));
    ASSERT_TRUE(
        RunLoopWithTimeoutOrUntil([&proc_terminated] { return proc_terminated; }, zx::sec(10)));
    zx_info_process_t info;
    zx_status_t status =
        zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(info.return_code, 0) << "Command failed with code " << info.return_code;

    ASSERT_TRUE(files::ReadFileDescriptorToString(outfd, out));
    close(outfd);
  }

  static void RunCommandAsync(const std::vector<const char*>& argv, int outfd, zx_handle_t* proc) {
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
    zx_status_t status =
        fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO, argv[0],
                       normalized_argv.data(), nullptr, std::size(actions), actions, proc, err_msg);
    ASSERT_EQ(status, ZX_OK) << "Failed to spawn command: " << err_msg;
  }

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> enclosing_env_;
  fidl::BindingSet<fuchsia::testing::chrealm::TestService> bindings_;
};

TEST_F(ChrealmTest, ConnectToService) {
  std::string realm_path;
  CreateRealm(&realm_path);

  // List services under /hub -- expect same as /svc.
  {
    // TODO(geb): Explicitly check for testing.chrealm.TestService once
    // fxbug.dev/3922 is fixed. Currently this trivially passes because readdir
    // only finds the built-in services.
    std::string svc_contents;
    std::string hub_contents;
    const std::vector<const char*> lssvc_args = {"/bin/chrealm", realm_path.c_str(), "--",
                                                 "/bin/ls", "/svc"};
    const std::vector<const char*> lshub_args = {"/bin/chrealm", realm_path.c_str(), "--",
                                                 "/bin/ls", "/hub/svc"};
    RunCommand(lssvc_args, &svc_contents);
    RunCommand(lshub_args, &hub_contents);
    EXPECT_EQ(svc_contents, hub_contents);
  }

  // Run chrealm under the test realm with the getmessage program, which
  // should attempt to reach TestService.
  {
    std::string out;
    const std::vector<const char*> args = {
        "/bin/chrealm", realm_path.c_str(), "--", "/bin/run",
        "fuchsia-pkg://fuchsia.com/chrealm_test_get_message#meta/"
        "chrealm_test_get_message.cmx"};
    RunCommand(args, &out);
    EXPECT_EQ(kMessage, out);
  }
}

TEST_F(ChrealmTest, CreatedUnderRealmJob) {
  std::string realm_path;
  CreateRealm(&realm_path);

  zx_handle_t proc = ZX_HANDLE_INVALID;
  const std::vector<const char*> args = {"/bin/chrealm", realm_path.c_str(), "--", "/bin/yes"};
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

  zx_status_t status =
      zx_object_wait_one(proc, ZX_PROCESS_TERMINATED, zx_deadline_after(ZX_SEC(10)), nullptr);
  ASSERT_EQ(ZX_OK, status);
}

}  // namespace
}  // namespace chrealm
