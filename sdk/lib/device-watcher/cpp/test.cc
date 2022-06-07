// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.device/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <zircon/time.h>

#include <memory>

#include <fbl/ref_ptr.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/pseudo_file.h"
#include "src/lib/storage/vfs/cpp/service.h"

TEST(DeviceWatcherTest, Smoke) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto file = fbl::MakeRefCounted<fs::UnbufferedPseudoFile>(
      [](fbl::String* output) { return ZX_OK; }, [](std::string_view input) { return ZX_OK; });

  auto third = fbl::MakeRefCounted<fs::PseudoDir>();
  third->AddEntry("file", file);

  auto second = fbl::MakeRefCounted<fs::PseudoDir>();
  second->AddEntry("third", std::move(third));

  auto first = fbl::MakeRefCounted<fs::PseudoDir>();
  first->AddEntry("second", std::move(second));
  first->AddEntry("file", file);

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_EQ(ZX_OK, endpoints.status_value());

  loop.StartThread();
  fs::ManagedVfs vfs(loop.dispatcher());

  vfs.ServeDirectory(first, std::move(endpoints->server));

  fbl::unique_fd dir;
  ASSERT_EQ(ZX_OK,
            fdio_fd_create(endpoints->client.TakeChannel().release(), dir.reset_and_get_address()));

  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::WaitForFile(dir, "file", &out));

  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(dir, "second/third/file", &out));

  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFileReadOnly(dir, "second/third/file", &out));

  sync_completion_t shutdown;

  vfs.Shutdown([&shutdown](zx_status_t status) {
    sync_completion_signal(&shutdown);
    ASSERT_EQ(status, ZX_OK);
  });
  ASSERT_EQ(sync_completion_wait(&shutdown, zx::duration::infinite().get()), ZX_OK);
}

TEST(DeviceWatcherTest, OpenInNamespace) {
  fbl::unique_fd f;
  ASSERT_EQ(device_watcher::RecursiveWaitForFileReadOnly("/dev/sys/test", &f), ZX_OK);
  ASSERT_EQ(device_watcher::RecursiveWaitForFile("/dev/sys/test", &f), ZX_OK);

  ASSERT_EQ(device_watcher::RecursiveWaitForFile("/other-test/file", &f), ZX_ERR_NOT_SUPPORTED);
}

constexpr std::string_view kTopoPath = "/dev/test/device/out";

class ControllerImpl : public fidl::testing::WireTestBase<fuchsia_device::Controller>,
                       public fs::Service {
 public:
  explicit ControllerImpl(std::string_view topo_path, async_dispatcher_t* dispatcher)
      : fs::Service([dispatcher, this](fidl::ServerEnd<fuchsia_device::Controller> server) {
          fidl::BindServer(dispatcher, std::move(server), this);
          return ZX_OK;
        }),
        topo_path_(topo_path) {}

  void GetTopologicalPath(GetTopologicalPathRequestView request,
                          GetTopologicalPathCompleter::Sync& completer) override {
    completer.ReplySuccess(fidl::StringView::FromExternal(topo_path_));
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    FAIL("Unexpected call to ControllerImpl: %s", name.c_str());
  }

 private:
  std::string topo_path_;
};

TEST(DeviceWatcherTest, WaitForDeviceTopologicalPath) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto empty_file = fbl::MakeRefCounted<fs::UnbufferedPseudoFile>(
      [](fbl::String* output) { return ZX_OK; }, [](std::string_view input) { return ZX_OK; });

  auto controller1 =
      fbl::MakeRefCounted<ControllerImpl>("/dev/test/not/the/one", loop.dispatcher());
  auto controller2 = fbl::MakeRefCounted<ControllerImpl>(kTopoPath, loop.dispatcher());

  auto first = fbl::MakeRefCounted<fs::PseudoDir>();
  first->AddEntry("file", empty_file);
  first->AddEntry("000", empty_file);
  first->AddEntry("001", controller1);
  first->AddEntry("002", controller2);
  first->AddEntry("003", empty_file);

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_EQ(ZX_OK, endpoints.status_value());

  loop.StartThread();
  fs::ManagedVfs vfs(loop.dispatcher());

  vfs.ServeDirectory(first, std::move(endpoints->server));

  fbl::unique_fd dir;
  ASSERT_EQ(ZX_OK,
            fdio_fd_create(endpoints->client.TakeChannel().release(), dir.reset_and_get_address()));

  zx::status<zx::channel> status =
      device_watcher::WaitForDeviceTopologicalPath(dir, std::string(kTopoPath).c_str());
  ASSERT_TRUE(status.is_ok());

  auto ctrl_client_end = fidl::ClientEnd<fuchsia_device::Controller>(std::move(status.value()));
  fidl::WireResult result = fidl::WireCall(ctrl_client_end)->GetTopologicalPath();
  ASSERT_EQ(kTopoPath, result->value()->path.get());
}
