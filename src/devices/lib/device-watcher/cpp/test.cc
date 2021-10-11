// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>

#include <memory>

#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

#include "src/devices/lib/device-watcher/cpp/device-watcher.h"
#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/pseudo_file.h"

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
