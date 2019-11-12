// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/txn_header.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include <fs/managed_vfs.h>
#include <fs/synchronous_vfs.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>
#include <unittest/unittest.h>

namespace {

class FdCountVnode : public fs::Vnode {
 public:
  FdCountVnode() : fd_count_(0) {}
  virtual ~FdCountVnode() { ZX_ASSERT(fd_count_ == 0); }

  int fds() const { return fd_count_; }

  zx_status_t Open(ValidatedOptions, fbl::RefPtr<Vnode>* redirect) final {
    fd_count_++;
    return ZX_OK;
  }

  zx_status_t Close() final {
    fd_count_--;
    ZX_ASSERT(fd_count_ >= 0);
    return ZX_OK;
  }

  fs::VnodeProtocolSet GetProtocols() const final { return fs::VnodeProtocol::kFile; }

  zx_status_t GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                     [[maybe_unused]] fs::Rights rights,
                                     fs::VnodeRepresentation* info) {
    *info = fs::VnodeRepresentation::Connector();
    return ZX_OK;
  }

 private:
  int fd_count_;
};

class AsyncTearDownVnode : public FdCountVnode {
 public:
  AsyncTearDownVnode(sync_completion_t* completions)
      : callback_(nullptr), completions_(completions) {}

  ~AsyncTearDownVnode() {
    // C) Tear down the Vnode.
    ZX_ASSERT(fds() == 0);
    sync_completion_signal(&completions_[2]);
  }

 private:
  void Sync(fs::Vnode::SyncCallback callback) final {
    callback_ = std::move(callback);
    thrd_t thrd;
    ZX_ASSERT(thrd_create(&thrd, &AsyncTearDownVnode::SyncThread, this) == thrd_success);
    thrd_detach(thrd);
  }

  static int SyncThread(void* arg) {
    fs::Vnode::SyncCallback callback;
    {
      fbl::RefPtr<AsyncTearDownVnode> vn = fbl::RefPtr(reinterpret_cast<AsyncTearDownVnode*>(arg));
      // A) Identify when the sync has started being processed.
      sync_completion_signal(&vn->completions_[0]);
      // B) Wait until the connection has been closed.
      sync_completion_wait(&vn->completions_[1], ZX_TIME_INFINITE);
      callback = std::move(vn->callback_);
    }
    callback(ZX_OK);
    return 0;
  }

  fs::Vnode::SyncCallback callback_;
  sync_completion_t* completions_;
};

bool send_sync(const zx::channel& client) {
  BEGIN_HELPER;
  fuchsia_io_NodeSyncRequest request;
  zx_txid_t txid = 5;
  fidl_init_txn_header(&request.hdr, txid, fuchsia_io_NodeSyncOrdinal);
  ASSERT_EQ(client.write(0, &request, sizeof(request), nullptr, 0), ZX_OK);
  END_HELPER;
}

// Helper function which creates a VFS with a served Vnode,
// starts a sync request, and then closes the connection to the client
// in the middle of the async callback.
//
// This helps tests get ready to try handling a tricky teardown.
bool sync_start(sync_completion_t* completions, async::Loop* loop,
                std::unique_ptr<fs::ManagedVfs>* vfs) {
  BEGIN_HELPER;
  *vfs = std::make_unique<fs::ManagedVfs>(loop->dispatcher());
  ASSERT_EQ(loop->StartThread(), ZX_OK);

  auto vn = fbl::AdoptRef(new AsyncTearDownVnode(completions));
  zx::channel client;
  zx::channel server;
  ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
  auto validated_options = vn->ValidateOptions(fs::VnodeConnectionOptions());
  ASSERT_TRUE(validated_options.is_ok());
  ASSERT_EQ(vn->Open(validated_options.value(), nullptr), ZX_OK);
  ASSERT_EQ((*vfs)->Serve(vn, std::move(server), validated_options.value()), ZX_OK);
  vn = nullptr;

  ASSERT_TRUE(send_sync(client));

  // A) Wait for sync to begin.
  sync_completion_wait(&completions[0], ZX_TIME_INFINITE);

  client.reset();
  END_HELPER;
}

// Test a case where the VFS object is shut down outside the dispatch loop.
bool TestUnpostedTeardown() {
  BEGIN_TEST;

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sync_completion_t completions[3];
  std::unique_ptr<fs::ManagedVfs> vfs;

  ASSERT_TRUE(sync_start(completions, &loop, &vfs));

  // B) Let sync complete.
  sync_completion_signal(&completions[1]);

  sync_completion_t* vnode_destroyed = &completions[2];
  sync_completion_t shutdown_done;
  vfs->Shutdown([&vnode_destroyed, &shutdown_done](zx_status_t status) {
    ZX_ASSERT(status == ZX_OK);
    // C) Issue an explicit shutdown, check that the Vnode has
    // already torn down.
    ZX_ASSERT(sync_completion_wait(vnode_destroyed, ZX_SEC(0)) == ZX_OK);
    sync_completion_signal(&shutdown_done);
  });
  ASSERT_EQ(sync_completion_wait(&shutdown_done, ZX_SEC(3)), ZX_OK);
  vfs = nullptr;

  END_TEST;
}

// Test a case where the VFS object is shut down as a posted request to the
// dispatch loop.
bool TestPostedTeardown() {
  BEGIN_TEST;

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sync_completion_t completions[3];
  std::unique_ptr<fs::ManagedVfs> vfs;

  ASSERT_TRUE(sync_start(completions, &loop, &vfs));

  // B) Let sync complete.
  sync_completion_signal(&completions[1]);

  sync_completion_t* vnode_destroyed = &completions[2];
  sync_completion_t shutdown_done;
  ASSERT_EQ(
      async::PostTask(loop.dispatcher(),
                      [&]() {
                        vfs->Shutdown([&vnode_destroyed, &shutdown_done](zx_status_t status) {
                          ZX_ASSERT(status == ZX_OK);
                          // C) Issue an explicit shutdown, check that the Vnode has
                          // already torn down.
                          ZX_ASSERT(sync_completion_wait(vnode_destroyed, ZX_SEC(0)) == ZX_OK);
                          sync_completion_signal(&shutdown_done);
                        });
                      }),
      ZX_OK);
  ASSERT_EQ(sync_completion_wait(&shutdown_done, ZX_SEC(3)), ZX_OK);
  vfs = nullptr;

  END_TEST;
}

// Test a case where the VFS object destroyed inside the callback to Shutdown.
bool TestTeardownDeleteThis() {
  BEGIN_TEST;

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sync_completion_t completions[3];
  std::unique_ptr<fs::ManagedVfs> vfs;

  ASSERT_TRUE(sync_start(completions, &loop, &vfs));

  // B) Let sync complete.
  sync_completion_signal(&completions[1]);

  sync_completion_t* vnode_destroyed = &completions[2];
  sync_completion_t shutdown_done;
  fs::ManagedVfs* raw_vfs = vfs.release();
  raw_vfs->Shutdown([&raw_vfs, &vnode_destroyed, &shutdown_done](zx_status_t status) {
    ZX_ASSERT(status == ZX_OK);
    // C) Issue an explicit shutdown, check that the Vnode has
    // already torn down.
    ZX_ASSERT(sync_completion_wait(vnode_destroyed, ZX_SEC(0)) == ZX_OK);
    delete raw_vfs;
    sync_completion_signal(&shutdown_done);
  });
  ASSERT_EQ(sync_completion_wait(&shutdown_done, ZX_SEC(3)), ZX_OK);

  END_TEST;
}

// Test a case where the VFS object is shut down before a background async
// callback gets the chance to complete.
bool TestTeardownSlowAsyncCallback() {
  BEGIN_TEST;

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sync_completion_t completions[3];
  std::unique_ptr<fs::ManagedVfs> vfs;

  ASSERT_TRUE(sync_start(completions, &loop, &vfs));

  sync_completion_t* vnode_destroyed = &completions[2];
  sync_completion_t shutdown_done;
  vfs->Shutdown([&vnode_destroyed, &shutdown_done](zx_status_t status) {
    ZX_ASSERT(status == ZX_OK);
    // C) Issue an explicit shutdown, check that the Vnode has
    // already torn down.
    //
    // Note: Will not be invoked until (B) completes.
    ZX_ASSERT(sync_completion_wait(vnode_destroyed, ZX_SEC(0)) == ZX_OK);
    sync_completion_signal(&shutdown_done);
  });

  // Shutdown should be waiting for our sync to finish.
  ASSERT_EQ(sync_completion_wait(&shutdown_done, ZX_MSEC(10)), ZX_ERR_TIMED_OUT);

  // B) Let sync complete.
  sync_completion_signal(&completions[1]);
  ASSERT_EQ(sync_completion_wait(&shutdown_done, ZX_SEC(3)), ZX_OK);
  vfs = nullptr;

  END_TEST;
}

// Test a case where the VFS object is shut down while a clone request
// is concurrently trying to open a new connection.
bool TestTeardownSlowClone() {
  BEGIN_TEST;

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sync_completion_t completions[3];
  auto vfs = std::make_unique<fs::ManagedVfs>(loop.dispatcher());
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  auto vn = fbl::AdoptRef(new AsyncTearDownVnode(completions));
  zx::channel client, server;
  ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
  auto validated_options = vn->ValidateOptions(fs::VnodeConnectionOptions());
  ASSERT_TRUE(validated_options.is_ok());
  ASSERT_EQ(vn->Open(validated_options.value(), nullptr), ZX_OK);
  ASSERT_EQ(vfs->Serve(vn, std::move(server), validated_options.value()), ZX_OK);
  vn = nullptr;

  // A) Wait for sync to begin.
  // Block the connection to the server in a sync, while simultaneously
  // sending a request to open a new connection.
  send_sync(client);
  sync_completion_wait(&completions[0], ZX_TIME_INFINITE);

  zx::channel client2, server2;
  ASSERT_EQ(zx::channel::create(0, &client2, &server2), ZX_OK);
  ASSERT_EQ(fuchsia_io_NodeClone(client.get(), 0, server2.release()), ZX_OK);

  // The connection is now:
  // - In a sync callback,
  // - Enqueued with a clone request,
  // - Closed.
  client.reset();

  sync_completion_t* vnode_destroyed = &completions[2];
  sync_completion_t shutdown_done;
  vfs->Shutdown([&vnode_destroyed, &shutdown_done](zx_status_t status) {
    ZX_ASSERT(status == ZX_OK);
    // C) Issue an explicit shutdown, check that the Vnode has
    // already torn down.
    //
    // Note: Will not be invoked until (B) completes.
    ZX_ASSERT(sync_completion_wait(vnode_destroyed, ZX_SEC(0)) == ZX_OK);
    sync_completion_signal(&shutdown_done);
  });

  // Shutdown should be waiting for our sync to finish.
  ASSERT_EQ(sync_completion_wait(&shutdown_done, ZX_MSEC(10)), ZX_ERR_TIMED_OUT);

  // B) Let sync complete. This should result in a successful termination
  // of the filesystem, even with the pending clone request.
  sync_completion_signal(&completions[1]);
  ASSERT_EQ(sync_completion_wait(&shutdown_done, ZX_SEC(3)), ZX_OK);
  vfs = nullptr;

  END_TEST;
}

bool TestSynchronousTeardown() {
  BEGIN_TEST;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  zx::channel client;

  {
    // Tear down the VFS while the async loop is running.
    auto vfs = std::make_unique<fs::SynchronousVfs>(loop.dispatcher());
    auto vn = fbl::AdoptRef(new FdCountVnode());
    zx::channel server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
    auto validated_options = vn->ValidateOptions(fs::VnodeConnectionOptions());
    ASSERT_TRUE(validated_options.is_ok());
    ASSERT_EQ(vn->Open(validated_options.value(), nullptr), ZX_OK);
    ASSERT_EQ(vfs->Serve(vn, std::move(server), validated_options.value()), ZX_OK);
  }

  loop.Quit();

  {
    // Tear down the VFS while the async loop is not running.
    auto vfs = std::make_unique<fs::SynchronousVfs>(loop.dispatcher());
    auto vn = fbl::AdoptRef(new FdCountVnode());
    zx::channel server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
    auto validated_options = vn->ValidateOptions(fs::VnodeConnectionOptions());
    ASSERT_TRUE(validated_options.is_ok());
    ASSERT_EQ(vn->Open(validated_options.value(), nullptr), ZX_OK);
    ASSERT_EQ(vfs->Serve(vn, std::move(server), validated_options.value()), ZX_OK);
  }

  {
    // Tear down the VFS with no active connections.
    auto vfs = std::make_unique<fs::SynchronousVfs>(loop.dispatcher());
  }

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(teardown_tests)
RUN_TEST(TestUnpostedTeardown)
RUN_TEST(TestPostedTeardown)
RUN_TEST(TestTeardownDeleteThis)
RUN_TEST(TestTeardownSlowAsyncCallback)
RUN_TEST(TestTeardownSlowClone)
RUN_TEST(TestSynchronousTeardown)
END_TEST_CASE(teardown_tests)
