// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/txn_header.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <memory>
#include <thread>
#include <utility>

#include <fs/managed_vfs.h>
#include <fs/synchronous_vfs.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>
#include <zxtest/zxtest.h>

namespace {

class FdCountVnode : public fs::Vnode {
 public:
  FdCountVnode() : fd_count_(0) {}
  virtual ~FdCountVnode() { EXPECT_EQ(0, fd_count_); }

  int fds() const { return fd_count_; }

  zx_status_t Open(ValidatedOptions, fbl::RefPtr<Vnode>* redirect) final {
    fd_count_++;
    return ZX_OK;
  }

  zx_status_t Close() final {
    fd_count_--;
    EXPECT_GE(fd_count_, 0);
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

// TODO(fxbug.dev/42589): Clean up the array-of-completions pattern.

class AsyncTearDownVnode : public FdCountVnode {
 public:
  AsyncTearDownVnode(sync_completion_t* completions, zx_status_t status_for_sync = ZX_OK)
      : callback_(nullptr), completions_(completions), status_for_sync_(status_for_sync) {}

  ~AsyncTearDownVnode() {
    // C) Tear down the Vnode.
    EXPECT_EQ(0, fds());
    sync_completion_signal(&completions_[2]);
  }

 private:
  void Sync(fs::Vnode::SyncCallback callback) final {
    callback_ = std::move(callback);
    std::thread thrd(&AsyncTearDownVnode::SyncThread, this);
    thrd.detach();
  }

  static void SyncThread(AsyncTearDownVnode* arg) {
    fs::Vnode::SyncCallback callback;
    zx_status_t status_for_sync;
    {
      fbl::RefPtr<AsyncTearDownVnode> vn = fbl::RefPtr(arg);
      status_for_sync = vn->status_for_sync_;
      // A) Identify when the sync has started being processed.
      sync_completion_signal(&vn->completions_[0]);
      // B) Wait until the connection has been closed.
      sync_completion_wait(&vn->completions_[1], ZX_TIME_INFINITE);
      callback = std::move(vn->callback_);
    }
    callback(status_for_sync);
  }

  fs::Vnode::SyncCallback callback_;
  sync_completion_t* completions_;
  zx_status_t status_for_sync_;
};

void SendSync(const zx::channel& client) {
  fidl::Buffer<llcpp::fuchsia::io::Node::SyncRequest> buffer;
  memset(buffer.view().begin(), 0, buffer.view().capacity());
  fidl::BytePart bytes = buffer.view();
  bytes.set_actual(bytes.capacity());
  new (bytes.data()) llcpp::fuchsia::io::Node::SyncRequest(5);
  fidl::DecodedMessage<llcpp::fuchsia::io::Node::SyncRequest> message(std::move(bytes));
  ASSERT_OK(fidl::Write(client, std::move(message)));
}

// Helper function which creates a VFS with a served Vnode,
// starts a sync request, and then closes the connection to the client
// in the middle of the async callback.
//
// This helps tests get ready to try handling a tricky teardown.
void SyncStart(sync_completion_t* completions, async::Loop* loop,
               std::unique_ptr<fs::ManagedVfs>* vfs, zx_status_t status_for_sync = ZX_OK) {
  *vfs = std::make_unique<fs::ManagedVfs>(loop->dispatcher());
  ASSERT_OK(loop->StartThread());

  auto vn = fbl::AdoptRef(new AsyncTearDownVnode(completions, status_for_sync));
  zx::channel client;
  zx::channel server;
  ASSERT_OK(zx::channel::create(0, &client, &server));
  auto validated_options = vn->ValidateOptions(fs::VnodeConnectionOptions());
  ASSERT_TRUE(validated_options.is_ok());
  ASSERT_OK(vn->Open(validated_options.value(), nullptr));
  ASSERT_OK((*vfs)->Serve(vn, std::move(server), validated_options.value()));
  vn = nullptr;

  ASSERT_NO_FAILURES(SendSync(client));

  // A) Wait for sync to begin.
  sync_completion_wait(&completions[0], ZX_TIME_INFINITE);

  client.reset();
}

void CommonTestUnpostedTeardown(zx_status_t status_for_sync) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sync_completion_t completions[3];
  std::unique_ptr<fs::ManagedVfs> vfs;

  ASSERT_NO_FAILURES(SyncStart(completions, &loop, &vfs, status_for_sync));

  // B) Let sync complete.
  sync_completion_signal(&completions[1]);

  sync_completion_t* vnode_destroyed = &completions[2];
  sync_completion_t shutdown_done;
  vfs->Shutdown([&vnode_destroyed, &shutdown_done](zx_status_t status) {
    ASSERT_OK(status);
    // C) Issue an explicit shutdown, check that the Vnode has
    // already torn down.
    ASSERT_OK(sync_completion_wait(vnode_destroyed, ZX_SEC(0)));
    sync_completion_signal(&shutdown_done);
  });
  ASSERT_OK(sync_completion_wait(&shutdown_done, ZX_SEC(3)));
}

// Test a case where the VFS object is shut down outside the dispatch loop.
TEST(Teardown, UnpostedTeardown) { CommonTestUnpostedTeardown(ZX_OK); }

// Test a case where the VFS object is shut down outside the dispatch loop,
// where the |Vnode::Sync| operation also failed causing the connection to
// be closed.
TEST(Teardown, UnpostedTeardownSyncError) { CommonTestUnpostedTeardown(ZX_ERR_INVALID_ARGS); }

void CommonTestPostedTeardown(zx_status_t status_for_sync) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sync_completion_t completions[3];
  std::unique_ptr<fs::ManagedVfs> vfs;

  ASSERT_NO_FAILURES(SyncStart(completions, &loop, &vfs, status_for_sync));

  // B) Let sync complete.
  sync_completion_signal(&completions[1]);

  sync_completion_t* vnode_destroyed = &completions[2];
  sync_completion_t shutdown_done;
  ASSERT_OK(async::PostTask(loop.dispatcher(), [&]() {
    vfs->Shutdown([&vnode_destroyed, &shutdown_done](zx_status_t status) {
      ASSERT_OK(status);
      // C) Issue an explicit shutdown, check that the Vnode has
      // already torn down.
      ASSERT_OK(sync_completion_wait(vnode_destroyed, ZX_SEC(0)));
      sync_completion_signal(&shutdown_done);
    });
  }));
  ASSERT_OK(sync_completion_wait(&shutdown_done, ZX_SEC(3)));
}

// Test a case where the VFS object is shut down as a posted request to the
// dispatch loop.
TEST(Teardown, PostedTeardown) { ASSERT_NO_FAILURES(CommonTestPostedTeardown(ZX_OK)); }

// Test a case where the VFS object is shut down as a posted request to the
// dispatch loop, where the |Vnode::Sync| operation also failed causing the
// connection to be closed.
TEST(Teardown, PostedTeardownSyncError) {
  ASSERT_NO_FAILURES(CommonTestPostedTeardown(ZX_ERR_INVALID_ARGS));
}

// Test a case where the VFS object destroyed inside the callback to Shutdown.
TEST(Teardown, TeardownDeleteThis) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sync_completion_t completions[3];
  std::unique_ptr<fs::ManagedVfs> vfs;

  ASSERT_NO_FAILURES(SyncStart(completions, &loop, &vfs));

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
  ASSERT_OK(sync_completion_wait(&shutdown_done, ZX_SEC(3)));
}

// Test a case where the VFS object is shut down before a background async
// callback gets the chance to complete.
TEST(Teardown, TeardownSlowAsyncCallback) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sync_completion_t completions[3];
  std::unique_ptr<fs::ManagedVfs> vfs;

  ASSERT_NO_FAILURES(SyncStart(completions, &loop, &vfs));

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
  ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&shutdown_done, ZX_MSEC(10)));

  // B) Let sync complete.
  sync_completion_signal(&completions[1]);
  ASSERT_OK(sync_completion_wait(&shutdown_done, ZX_SEC(3)));
}

// Test a case where the VFS object is shut down while a clone request
// is concurrently trying to open a new connection.
TEST(Teardown, TeardownSlowClone) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sync_completion_t completions[3];
  auto vfs = std::make_unique<fs::ManagedVfs>(loop.dispatcher());
  ASSERT_OK(loop.StartThread());

  auto vn = fbl::AdoptRef(new AsyncTearDownVnode(completions));
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));
  auto validated_options = vn->ValidateOptions(fs::VnodeConnectionOptions());
  ASSERT_TRUE(validated_options.is_ok());
  ASSERT_OK(vn->Open(validated_options.value(), nullptr));
  ASSERT_OK(vfs->Serve(vn, std::move(server), validated_options.value()));
  vn = nullptr;

  // A) Wait for sync to begin.
  // Block the connection to the server in a sync, while simultaneously
  // sending a request to open a new connection.
  SendSync(client);
  sync_completion_wait(&completions[0], ZX_TIME_INFINITE);

  zx::channel client2, server2;
  ASSERT_OK(zx::channel::create(0, &client2, &server2));
  llcpp::fuchsia::io::Node::SyncClient fidl_client2(std::move(client2));
  ASSERT_OK(fidl_client2.Clone(0, std::move(server2)).status());

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
  ASSERT_EQ(ZX_ERR_TIMED_OUT, sync_completion_wait(&shutdown_done, ZX_MSEC(10)));

  // B) Let sync complete. This should result in a successful termination
  // of the filesystem, even with the pending clone request.
  sync_completion_signal(&completions[1]);
  ASSERT_OK(sync_completion_wait(&shutdown_done, ZX_SEC(3)));
}

TEST(Teardown, SynchronousTeardown) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  zx::channel client;

  {
    // Tear down the VFS while the async loop is running.
    auto vfs = std::make_unique<fs::SynchronousVfs>(loop.dispatcher());
    auto vn = fbl::AdoptRef(new FdCountVnode());
    zx::channel server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    auto validated_options = vn->ValidateOptions(fs::VnodeConnectionOptions());
    ASSERT_TRUE(validated_options.is_ok());
    ASSERT_OK(vn->Open(validated_options.value(), nullptr));
    ASSERT_OK(vfs->Serve(vn, std::move(server), validated_options.value()));
  }

  loop.Quit();

  {
    // Tear down the VFS while the async loop is not running.
    auto vfs = std::make_unique<fs::SynchronousVfs>(loop.dispatcher());
    auto vn = fbl::AdoptRef(new FdCountVnode());
    zx::channel server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    auto validated_options = vn->ValidateOptions(fs::VnodeConnectionOptions());
    ASSERT_TRUE(validated_options.is_ok());
    ASSERT_OK(vn->Open(validated_options.value(), nullptr));
    ASSERT_OK(vfs->Serve(vn, std::move(server), validated_options.value()));
  }

  {
    // Tear down the VFS with no active connections.
    auto vfs = std::make_unique<fs::SynchronousVfs>(loop.dispatcher());
  }
}

}  // namespace
