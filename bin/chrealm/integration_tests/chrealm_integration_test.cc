// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <glob.h>
#include <string>
#include <unistd.h>
#include <vector>

#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/testing/chrealm/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/spawn.h>
#include <zircon/compiler.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "garnet/bin/appmgr/util.h"
#include "gtest/gtest.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/split_string.h"
#include "lib/svc/cpp/services.h"

namespace chrealm {
namespace {

void RunCommandV(const std::vector<const char*>& argv, std::string* out) {
  *out = "";

  std::vector<const char*> normalized_argv = argv;
  normalized_argv.push_back(nullptr);

  FILE* tmpf = std::tmpfile();
  if (tmpf == nullptr) {
    FAIL() << "Could not open temp file";
  }
  const int tmpfd = fileno(tmpf);
  // Redirect process's stdout to file.
  fdio_spawn_action_t actions[] = {
      {.action = FDIO_SPAWN_ACTION_CLONE_FD,
       .fd = {.local_fd = tmpfd, .target_fd = STDOUT_FILENO}
      },
      {.action = FDIO_SPAWN_ACTION_CLONE_FD,
       .fd = {.local_fd = STDIN_FILENO, .target_fd = STDIN_FILENO}
      },
      {.action = FDIO_SPAWN_ACTION_CLONE_FD,
       .fd = {.local_fd = STDERR_FILENO, .target_fd = STDERR_FILENO}
      }
  };
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_handle_t proc = ZX_HANDLE_INVALID;
  zx_status_t status = fdio_spawn_etc(
      ZX_HANDLE_INVALID,
      FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO, argv[0],
      normalized_argv.data(), nullptr, countof(actions), actions, &proc,
      err_msg);
  ASSERT_EQ(status, ZX_OK) << "Failed to run command: " << err_msg;
  status = zx_object_wait_one(proc, ZX_PROCESS_TERMINATED,
                              zx_deadline_after(ZX_SEC(10)), nullptr);
  ASSERT_EQ(status, ZX_OK);
  zx_info_process_t info;
  status =
      zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), nullptr,
                         nullptr);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(info.return_code, 0)
      << "Command failed with status "
      << zx_status_get_string(info.return_code);

  ASSERT_TRUE(files::ReadFileDescriptorToString(tmpfd, out));
  close(tmpfd);
}

const char kMessage[] = "hello";
const char kRealm[] = "chrealmtest";

class ChrealmTest : public ::testing::Test,
                    public fuchsia::testing::chrealm::TestService {
 public:
  void GetMessage(fuchsia::testing::chrealm::TestService::GetMessageCallback cb)
      override {
    cb(kMessage);
  }

 protected:
  ChrealmTest()
      : loop_(&kAsyncLoopConfigMakeDefault),
        vfs_(async_get_default()),
        services_(fbl::AdoptRef(new fs::PseudoDir)) {
    fuchsia::sys::ConnectToEnvironmentService(sys_env_.NewRequest());
    sys_env_->GetServices(svc_.NewRequest());
    // Add a TestService that the test realm can use.
    services_->AddEntry(
        fuchsia::testing::chrealm::TestService::Name_,
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          bindings_.AddBinding(
              this,
              fidl::InterfaceRequest<fuchsia::testing::chrealm::TestService>(
                  std::move(channel)));
          return ZX_OK;
        })));
    // Loader service is needed for 'run'.
    services_->AddEntry(
        fuchsia::sys::Loader::Name_,
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          svc_->ConnectToService(fuchsia::sys::Loader::Name_,
                                 std::move(channel));
          return ZX_OK;
        })));

    // Run a loop that services_ can use.
    loop_.StartThread();
  }

  ~ChrealmTest() {
    if (controller_) {
      controller_->Kill();
    }
  }

  void CreateNestedEnvironment(
      const std::string& label,
      fuchsia::sys::EnvironmentSyncPtr* nested_env_out) {
    ASSERT_FALSE(created_realm_) << "Can only create realm once.";
    sys_env_->CreateNestedEnvironment(
        component::Util::OpenAsDirectory(&vfs_, services_),
        nested_env_out->NewRequest(),
        controller_.NewRequest(), label);
    created_realm_ = true;
  }

 private:
  async::Loop loop_;
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> services_;
  fuchsia::sys::EnvironmentSyncPtr sys_env_;
  fuchsia::sys::ServiceProviderSyncPtr svc_;
  fuchsia::sys::EnvironmentControllerSyncPtr controller_;
  fidl::BindingSet<fuchsia::testing::chrealm::TestService> bindings_;
  bool created_realm_ = false;
};

TEST_F(ChrealmTest, ConnectToService) {
  static const char kRealmGlob[] = "/hub/r/sys/*/r/chrealmtest/*";

  // Verify the realm doesn't exist to start with.
  glob_t globbuf;
  ASSERT_EQ(GLOB_NOMATCH, glob(kRealmGlob, 0, nullptr, &globbuf));

  // Create a nested realm to test with.
  fuchsia::sys::EnvironmentSyncPtr nested_env;
  CreateNestedEnvironment(kRealm, &nested_env);

  // Get the path to the test realm in /hub. Test is running in the root
  // realm, so we find the realm under sys.
  ASSERT_EQ(0, glob(kRealmGlob, 0, nullptr, &globbuf))
      << "glob found no matches";
  ASSERT_EQ(1u, globbuf.gl_pathc);
  const std::string realm_path = globbuf.gl_pathv[0];
  globfree(&globbuf);

  // Run chrealm under the test realm with the getmessage program, which
  // should attempt to reach TestService.
  std::string out;
  const std::vector<const char *> args =
      {"/system/bin/chrealm", realm_path.c_str(), "--",
       "/system/bin/run", "chrealm_test_get_message"};
  RunCommandV(args, &out);
  EXPECT_EQ(kMessage, out);
}

}  // namespace
}  // namespace chrealm
