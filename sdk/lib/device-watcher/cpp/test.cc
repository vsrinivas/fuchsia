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

#include "lib/async-loop/loop.h"
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

  void GetTopologicalPath(GetTopologicalPathCompleter::Sync& completer) override {
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

  zx::result<zx::channel> status =
      device_watcher::WaitForDeviceTopologicalPath(dir, std::string(kTopoPath).c_str());
  ASSERT_TRUE(status.is_ok());

  auto ctrl_client_end = fidl::ClientEnd<fuchsia_device::Controller>(std::move(status.value()));
  fidl::WireResult result = fidl::WireCall(ctrl_client_end)->GetTopologicalPath();
  ASSERT_EQ(kTopoPath, result->value()->path.get());

  sync_completion_t shutdown;

  vfs.Shutdown([&shutdown](zx_status_t status) {
    sync_completion_signal(&shutdown);
    ASSERT_EQ(status, ZX_OK);
  });
  ASSERT_EQ(sync_completion_wait(&shutdown, zx::duration::infinite().get()), ZX_OK);
}

class IterateDirectoryTest : public zxtest::Test {
 public:
  IterateDirectoryTest()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), vfs_(loop_.dispatcher()) {}

  void SetUp() override {
    // Set up the fake filesystem.
    auto file1 = fbl::MakeRefCounted<fs::UnbufferedPseudoFile>(
        [](fbl::String* output) { return ZX_OK; }, [](std::string_view input) { return ZX_OK; });
    auto file2 = fbl::MakeRefCounted<fs::UnbufferedPseudoFile>(
        [](fbl::String* output) { return ZX_OK; }, [](std::string_view input) { return ZX_OK; });

    auto first = fbl::MakeRefCounted<fs::PseudoDir>();
    first->AddEntry("file1", file1);
    first->AddEntry("file2", file2);

    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_EQ(ZX_OK, endpoints.status_value());

    loop_.StartThread();
    vfs_.ServeDirectory(first, std::move(endpoints->server));

    ASSERT_EQ(ZX_OK, fdio_fd_create(endpoints->client.TakeChannel().release(),
                                    dir_.reset_and_get_address()));
  }

  void TearDown() override {
    sync_completion_t shutdown;
    vfs_.Shutdown([&shutdown](zx_status_t status) {
      sync_completion_signal(&shutdown);
      ASSERT_EQ(status, ZX_OK);
    });
    ASSERT_EQ(ZX_OK, sync_completion_wait(&shutdown, zx::duration::infinite().get()));
    loop_.Shutdown();
  }

 protected:
  async::Loop loop_;
  fs::ManagedVfs vfs_;
  fbl::unique_fd dir_;
};

TEST_F(IterateDirectoryTest, IterateDirectory) {
  std::vector<std::string> seen;
  zx_status_t status = device_watcher::IterateDirectory(
      std::move(dir_), [&seen](std::string_view filename, zx::channel channel) {
        // Collect the file names into the vector.
        seen.emplace_back(filename);
        return ZX_OK;
      });
  ASSERT_EQ(ZX_OK, status);

  // Make sure the file names seen were as expected.
  ASSERT_EQ(2, seen.size());
  std::sort(seen.begin(), seen.end());
  ASSERT_EQ("file1", seen[0]);
  ASSERT_EQ("file2", seen[1]);
}

TEST_F(IterateDirectoryTest, IterateDirectoryCancelled) {
  // Test that iteration is cancelled when the callback returns an error
  std::vector<std::string> seen;
  zx_status_t status = device_watcher::IterateDirectory(
      std::move(dir_), [&seen](std::string_view filename, zx::channel channel) {
        seen.emplace_back(filename);
        return ZX_ERR_INTERNAL;
      });
  ASSERT_EQ(ZX_ERR_INTERNAL, status);

  // Should only have seen a single file before exiting.
  ASSERT_EQ(1, seen.size());
}

TEST_F(IterateDirectoryTest, IterateDirectoryChannel) {
  // Test that we can use the channel passed to the callback function to make
  // fuchsia.io.Node calls.
  std::vector<uint64_t> content_sizes;
  zx_status_t status = device_watcher::IterateDirectory(
      std::move(dir_), [&content_sizes](std::string_view filename, zx::channel channel) {
        auto result =
            fidl::WireCall(fidl::UnownedClientEnd<fuchsia_io::Node>(channel.borrow()))->GetAttr();
        if (!result.ok()) {
          return result.status();
        }
        content_sizes.push_back(result.value().attributes.content_size);
        return ZX_OK;
      });
  ASSERT_EQ(ZX_OK, status);

  ASSERT_EQ(2, content_sizes.size());

  // Files are empty.
  ASSERT_EQ(0, content_sizes[0]);
  ASSERT_EQ(0, content_sizes[1]);
}
