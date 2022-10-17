// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/device-name-provider/args.h"

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl-async/cpp/bind.h>

#include <mock-boot-arguments/server.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace {
constexpr char kInterface[] = "/dev/whatever/whatever";
constexpr char kNodename[] = "some-four-word-name";

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
  fidl::UnownedClientEnd<fuchsia_io::Directory> svc_chan() const { return svc_local_; }

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
  fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root() const { return fake_svc_.svc_chan(); }

 private:
  async::Loop loop_;
  FakeSvc fake_svc_;
};

TEST_F(ArgsTest, DeviceNameProviderNoneProvided) {
  int argc = 1;
  const char* argv[] = {"device-name-provider"};
  const char* error = nullptr;
  DeviceNameProviderArgs args;
  ASSERT_EQ(ParseArgs(argc, const_cast<char**>(argv), svc_root(), &error, &args), 0, "%s", error);
  ASSERT_TRUE(args.interface.empty());
  ASSERT_TRUE(args.nodename.empty());
  ASSERT_EQ(args.namegen, 1);
  ASSERT_EQ(args.devdir, kDefaultDevdir);
  ASSERT_EQ(error, nullptr);
}

TEST_F(ArgsTest, DeviceNameProviderAllProvided) {
  int argc = 9;
  constexpr char kDevDir[] = "/foo";
  const char* argv[] = {"device-name-provider",
                        "--nodename",
                        kNodename,
                        "--interface",
                        kInterface,
                        "--devdir",
                        kDevDir,
                        "--namegen",
                        "0"};
  const char* error = nullptr;
  DeviceNameProviderArgs args;
  ASSERT_EQ(ParseArgs(argc, const_cast<char**>(argv), svc_root(), &error, &args), 0, "%s", error);
  ASSERT_EQ(args.interface, std::string(kInterface));
  ASSERT_EQ(args.nodename, std::string(kNodename));
  ASSERT_EQ(args.devdir, std::string(kDevDir));
  ASSERT_EQ(args.namegen, 0);
  ASSERT_EQ(error, nullptr);
}

TEST_F(ArgsTest, DeviceNameProviderValidation) {
  int argc = 2;
  const char* argv[] = {
      "device-name-provider",
      "--interface",
  };
  DeviceNameProviderArgs args;
  const char* error = nullptr;
  ASSERT_LT(ParseArgs(argc, const_cast<char**>(argv), svc_root(), &error, &args), 0);
  ASSERT_TRUE(args.interface.empty());
  ASSERT_TRUE(strstr(error, "interface"));

  argc = 2;
  argv[1] = "--nodename";
  args.interface = "";
  args.nodename = "";
  args.namegen = 1;
  error = nullptr;
  ASSERT_LT(ParseArgs(argc, const_cast<char**>(argv), svc_root(), &error, &args), 0);
  ASSERT_TRUE(args.nodename.empty());
  ASSERT_TRUE(strstr(error, "nodename"));

  argc = 2;
  argv[1] = "--namegen";
  args.interface = "";
  args.nodename = "";
  args.namegen = 1;
  error = nullptr;
  ASSERT_LT(ParseArgs(argc, const_cast<char**>(argv), svc_root(), &error, &args), 0);
  ASSERT_EQ(args.namegen, 1);
  ASSERT_TRUE(strstr(error, "namegen"));
}
}  // namespace
