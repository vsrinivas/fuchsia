// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-testing/test_loop.h>

#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace {

// Simple vnode implementation that provides a way to query whether the vfs pointer is set.
class TestNode : public fs::Vnode {
 public:
  // Vnode implementation:
  fs::VnodeProtocolSet GetProtocols() const override { return fs::VnodeProtocol::kFile; }
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights,
                                     fs::VnodeRepresentation* info) final {
    if (protocol == fs::VnodeProtocol::kFile) {
      *info = fs::VnodeRepresentation::File();
      return ZX_OK;
    }
    return ZX_ERR_NOT_SUPPORTED;
  }

  bool HasVfsPointer() {
    std::lock_guard lock(mutex_);
    return !!vfs();
  }

 private:
  friend fbl::internal::MakeRefCountedHelper<TestNode>;
  friend fbl::RefPtr<TestNode>;

  explicit TestNode(fs::Vfs* vfs) : Vnode(vfs) {}
  ~TestNode() override {}
};

}  // namespace

// ManagedVfs always sets the dispatcher in its constructor, and trying to change it using
// Vfs::SetDispatcher should fail.
TEST(ManagedVfs, CantSetDispatcher) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fs::ManagedVfs vfs(loop.dispatcher());
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

  auto result = fidl::WireCall<fuchsia_io::DirectoryAdmin>(zx::unowned_channel{local}).Unmount();
  ASSERT_OK(result.status());
  ASSERT_OK(result->s);
  ASSERT_TRUE(vfs.IsTerminating());
}

TEST(ManagedVfs, UnmountAndShutdown) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fs::ManagedVfs vfs(loop.dispatcher());
  loop.StartThread();

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
  ASSERT_OK(vfs.ServeDirectory(std::move(dir), std::move(remote)));

  auto result = fidl::WireCall<fuchsia_io::DirectoryAdmin>(zx::unowned_channel{local}).Unmount();
  ASSERT_OK(result.status());
  ASSERT_OK(result->s);
  ASSERT_TRUE(vfs.IsTerminating());
}

static void CheckClosesConnection(fs::FuchsiaVfs* vfs, async::TestLoop* loop) {
  zx::channel local_a, remote_a, local_b, remote_b;
  ASSERT_OK(zx::channel::create(0, &local_a, &remote_a));
  ASSERT_OK(zx::channel::create(0, &local_b, &remote_b));

  auto dir_a = fbl::MakeRefCounted<fs::PseudoDir>();
  auto dir_b = fbl::MakeRefCounted<fs::PseudoDir>();
  ASSERT_OK(vfs->ServeDirectory(dir_a, std::move(remote_a)));
  ASSERT_OK(vfs->ServeDirectory(dir_b, std::move(remote_b)));
  bool callback_called = false;
  vfs->CloseAllConnectionsForVnode(*dir_a, [&callback_called]() { callback_called = true; });
  loop->RunUntilIdle();
  zx_signals_t signals;
  ASSERT_OK(local_a.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &signals));
  ASSERT_TRUE(signals & ZX_CHANNEL_PEER_CLOSED);
  ASSERT_EQ(ZX_ERR_TIMED_OUT, local_b.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time(0), &signals));
  ASSERT_TRUE(callback_called);
}

TEST(ManagedVfs, CloseAllConnections) {
  async::TestLoop loop;
  fs::ManagedVfs vfs(loop.dispatcher());
  CheckClosesConnection(&vfs, &loop);
  loop.RunUntilIdle();
}

TEST(SynchronousVfs, CloseAllConnections) {
  async::TestLoop loop;
  fs::SynchronousVfs vfs(loop.dispatcher());
  CheckClosesConnection(&vfs, &loop);
  loop.RunUntilIdle();
}

TEST(ManagedVfs, CloseAllConnectionsForVnodeWithoutAnyConnections) {
  async::TestLoop loop;
  fs::ManagedVfs vfs(loop.dispatcher());
  auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
  bool closed = false;
  vfs.CloseAllConnectionsForVnode(*dir, [&closed]() { closed = true; });
  loop.RunUntilIdle();
  ASSERT_TRUE(closed);
}

TEST(SynchronousVfs, CloseAllConnectionsForVnodeWithoutAnyConnections) {
  async::TestLoop loop;
  fs::SynchronousVfs vfs(loop.dispatcher());
  auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
  bool closed = false;
  vfs.CloseAllConnectionsForVnode(*dir, [&closed]() { closed = true; });
  loop.RunUntilIdle();
  ASSERT_TRUE(closed);
}

TEST(SynchronousVfs, DeletesNodeVfsPointers) {
  async::TestLoop loop;
  auto vfs = std::make_unique<fs::SynchronousVfs>(loop.dispatcher());

  auto file = fbl::MakeRefCounted<TestNode>(vfs.get());
  EXPECT_TRUE(file->HasVfsPointer());

  // Delete the Vfs while keeping the file alive after it.
  vfs.reset();
  EXPECT_FALSE(file->HasVfsPointer());
}
