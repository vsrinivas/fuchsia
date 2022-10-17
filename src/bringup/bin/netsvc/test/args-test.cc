// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/args.h"

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>
#include <lib/zx/process.h>

#include <mock-boot-arguments/server.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace {
constexpr char kInterface[] = "/dev/whatever/whatever";

TEST(ArgsTest, NetsvcNodenamePrintsAndExits) {
  const std::string path = "/pkg/bin/netsvc";
  const char* argv[] = {path.c_str(), "--nodename", nullptr};
  zx::process process;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];

  ASSERT_OK(fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv, nullptr, 0,
                           nullptr, process.reset_and_get_address(), err_msg),
            "%s", err_msg);

  ASSERT_OK(process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));
  zx_info_process_t proc_info;
  ASSERT_OK(process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr));
  ASSERT_EQ(proc_info.return_code, 0);
}

class FakeSvc {
 public:
  explicit FakeSvc(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher), vfs_(dispatcher) {
    auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
    root_dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_boot::Arguments>,
                       fbl::MakeRefCounted<fs::Service>(
                           [this](fidl::ServerEnd<fuchsia_boot::Arguments> server_end) {
                             fidl::BindServer(dispatcher_, std::move(server_end), &mock_boot_);
                             return ZX_OK;
                           }));

    zx::result server_end = fidl::CreateEndpoints(&svc_local_);
    ASSERT_OK(server_end.status_value());
    vfs_.ServeDirectory(root_dir, std::move(server_end.value()));
  }

  mock_boot_arguments::Server& mock_boot() { return mock_boot_; }
  fidl::UnownedClientEnd<fuchsia_io::Directory> svc_chan() { return svc_local_; }

 private:
  async_dispatcher_t* dispatcher_;
  fs::SynchronousVfs vfs_;
  mock_boot_arguments::Server mock_boot_;
  fidl::ClientEnd<fuchsia_io::Directory> svc_local_;
};

class ArgsTest : public zxtest::Test {
 public:
  ArgsTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), fake_svc_(loop_.dispatcher()) {
    loop_.StartThread("paver-test-loop");
  }

  ~ArgsTest() { loop_.Shutdown(); }

  FakeSvc& fake_svc() { return fake_svc_; }
  fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root() { return fake_svc_.svc_chan(); }

 private:
  async::Loop loop_;
  FakeSvc fake_svc_;
};

TEST_F(ArgsTest, NetsvcNoneProvided) {
  int argc = 1;
  const char* argv[] = {"netsvc"};
  const char* error = nullptr;
  NetsvcArgs args;
  ASSERT_EQ(ParseArgs(argc, const_cast<char**>(argv), svc_root(), &error, &args), 0, "%s", error);
  ASSERT_FALSE(args.netboot);
  ASSERT_FALSE(args.print_nodename_and_exit);
  ASSERT_TRUE(args.advertise);
  ASSERT_FALSE(args.all_features);
  ASSERT_TRUE(args.interface.empty());
  ASSERT_EQ(error, nullptr);
}

TEST_F(ArgsTest, NetsvcAllProvided) {
  int argc = 7;
  const char* argv[] = {
      "netsvc",         "--netboot",   "--nodename", "--advertise",
      "--all-features", "--interface", kInterface,
  };
  const char* error = nullptr;
  NetsvcArgs args;
  ASSERT_EQ(ParseArgs(argc, const_cast<char**>(argv), svc_root(), &error, &args), 0, "%s", error);
  ASSERT_TRUE(args.netboot);
  ASSERT_TRUE(args.print_nodename_and_exit);
  ASSERT_TRUE(args.advertise);
  ASSERT_TRUE(args.all_features);
  ASSERT_EQ(args.interface, std::string(kInterface));
  ASSERT_EQ(error, nullptr);
}

TEST_F(ArgsTest, NetsvcValidation) {
  int argc = 2;
  const char* argv[] = {
      "netsvc",
      "--interface",
  };
  const char* error = nullptr;
  NetsvcArgs args;
  ASSERT_LT(ParseArgs(argc, const_cast<char**>(argv), svc_root(), &error, &args), 0);
  ASSERT_TRUE(args.interface.empty());
  ASSERT_TRUE(strstr(error, "interface"));
}

TEST_F(ArgsTest, LogPackets) {
  int argc = 2;
  const char* argv[] = {
      "netsvc",
      "--log-packets",
  };
  NetsvcArgs args;
  EXPECT_FALSE(args.log_packets);
  const char* error = nullptr;
  ASSERT_EQ(ParseArgs(argc, const_cast<char**>(argv), svc_root(), &error, &args), 0, "%s", error);
  EXPECT_TRUE(args.log_packets);
}

}  // namespace
