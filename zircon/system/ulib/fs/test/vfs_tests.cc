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

static void CheckClosesConnection(fs::Vfs* vfs) {
  zx::channel local_a, remote_a, local_b, remote_b;
  ASSERT_OK(zx::channel::create(0, &local_a, &remote_a));
  ASSERT_OK(zx::channel::create(0, &local_b, &remote_b));

  auto dir_a = fbl::MakeRefCounted<fs::PseudoDir>();
  auto dir_b = fbl::MakeRefCounted<fs::PseudoDir>();
  ASSERT_OK(vfs->ServeDirectory(dir_a, std::move(remote_a)));
  ASSERT_OK(vfs->ServeDirectory(dir_b, std::move(remote_b)));
  vfs->CloseAllConnectionsForVnode(*dir_a);
  zx_signals_t signals;
  ASSERT_OK(local_a.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &signals));
  ASSERT_TRUE(signals & ZX_CHANNEL_PEER_CLOSED);
  ASSERT_EQ(ZX_ERR_TIMED_OUT, local_b.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time(0), &signals));
}

TEST(ManagedVfs, CloseAllConnections) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fs::ManagedVfs vfs(loop.dispatcher());
  loop.StartThread();
  CheckClosesConnection(&vfs);
  loop.Shutdown();
}

TEST(SynchronousVfs, CloseAllConnections) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fs::SynchronousVfs vfs(loop.dispatcher());
  loop.StartThread();
  CheckClosesConnection(&vfs);
  loop.Shutdown();
}
