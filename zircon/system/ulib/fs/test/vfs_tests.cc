// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <fs/managed_vfs.h>
#include <fs/pseudo_dir.h>
#include <fs/synchronous_vfs.h>
#include <zxtest/zxtest.h>

TEST(ManagedVfs, CanOnlySetDispatcherOnce) {
  fs::ManagedVfs vfs;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  vfs.SetDispatcher(loop.dispatcher());

  ASSERT_DEATH([&]() { vfs.SetDispatcher(loop.dispatcher()); });
}

TEST(SynchronousVfs, CanOnlySetDispatcherOnce) {
  fs::SynchronousVfs vfs;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  vfs.SetDispatcher(loop.dispatcher());

  ASSERT_DEATH([&]() { vfs.SetDispatcher(loop.dispatcher()); });
}

TEST(SynchronousVfs, UnmountAndShutdown) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fs::SynchronousVfs vfs(loop.dispatcher());
  loop.StartThread();

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
  ASSERT_OK(vfs.ServeDirectory(std::move(dir), std::move(remote)));

  auto result = llcpp::fuchsia::io::DirectoryAdmin::Call::Unmount(zx::unowned_channel{local});
  ASSERT_OK(result.status());
  ASSERT_OK(result->s);
  ASSERT_TRUE(static_cast<fs::Vfs*>(&vfs)->IsTerminating());
}

TEST(ManagedVfs, UnmountAndShutdown) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fs::ManagedVfs vfs(loop.dispatcher());
  loop.StartThread();

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
  ASSERT_OK(vfs.ServeDirectory(std::move(dir), std::move(remote)));

  auto result = llcpp::fuchsia::io::DirectoryAdmin::Call::Unmount(zx::unowned_channel{local});
  ASSERT_OK(result.status());
  ASSERT_OK(result->s);
  ASSERT_TRUE(static_cast<fs::Vfs*>(&vfs)->IsTerminating());
}
