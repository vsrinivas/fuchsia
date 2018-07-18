// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/managed-vfs.h>
#include <fs/synchronous-vfs.h>
#include <fs/vnode.h>
#include <fs/vfs.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/channel.h>
#include <sync/completion.h>
#include <zircon/assert.h>

#include <unittest/unittest.h>

// Used to minimize boilerplate for invoking FIDL requests
// under highly controlled situations.
#include <lib/fdio/../../../private-fidl.h>

namespace {

class FdCountVnode : public fs::Vnode {
public:
    FdCountVnode() : fd_count_(0) {}
    virtual ~FdCountVnode() {
        ZX_ASSERT(fd_count_ == 0);
    }

    int fds() const {
        return fd_count_;
    }

    zx_status_t Open(uint32_t, fbl::RefPtr<Vnode>* redirect) final {
        fd_count_++;
        return ZX_OK;
    }

    zx_status_t Close() final {
        fd_count_--;
        ZX_ASSERT(fd_count_ >= 0);
        return ZX_OK;
    }

private:
    int fd_count_;
};

class AsyncTearDownVnode : public FdCountVnode {
public:
    AsyncTearDownVnode(completion_t* completions) :
        callback_(nullptr), completions_(completions) {}

    ~AsyncTearDownVnode() {
        // C) Tear down the Vnode.
        ZX_ASSERT(fds() == 0);
        completion_signal(&completions_[2]);
    }

private:
    void Sync(fs::Vnode::SyncCallback callback) final {
        callback_ = fbl::move(callback);
        thrd_t thrd;
        ZX_ASSERT(thrd_create(&thrd, &AsyncTearDownVnode::SyncThread, this) == thrd_success);
        thrd_detach(thrd);
    }

    static int SyncThread(void* arg) {
        fs::Vnode::SyncCallback callback;
        {
            fbl::RefPtr<AsyncTearDownVnode> vn =
                    fbl::WrapRefPtr(reinterpret_cast<AsyncTearDownVnode*>(arg));
            // A) Identify when the sync has started being processed.
            completion_signal(&vn->completions_[0]);
            // B) Wait until the connection has been closed.
            completion_wait(&vn->completions_[1], ZX_TIME_INFINITE);
            callback = fbl::move(vn->callback_);
        }
        callback(ZX_OK);
        return 0;
    }

    fs::Vnode::SyncCallback callback_;
    completion_t* completions_;
};

bool send_sync(const zx::channel& client) {
    BEGIN_HELPER;
    fuchsia_io_NodeSyncRequest request;
    request.hdr.txid = 5;
    request.hdr.ordinal = ZXFIDL_SYNC;
    ASSERT_EQ(client.write(0, &request, sizeof(request), nullptr, 0), ZX_OK);
    END_HELPER;
}

// Helper function which creates a VFS with a served Vnode,
// starts a sync request, and then closes the connection to the client
// in the middle of the async callback.
//
// This helps tests get ready to try handling a tricky teardown.
bool sync_start(completion_t* completions, async::Loop* loop,
                fbl::unique_ptr<fs::ManagedVfs>* vfs) {
    BEGIN_HELPER;
    *vfs = fbl::make_unique<fs::ManagedVfs>(loop->dispatcher());
    ASSERT_EQ(loop->StartThread(), ZX_OK);

    auto vn = fbl::AdoptRef(new AsyncTearDownVnode(completions));
    zx::channel client;
    zx::channel server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
    ASSERT_EQ(vn->Open(0, nullptr), ZX_OK);
    ASSERT_EQ(vn->Serve(vfs->get(), fbl::move(server), 0), ZX_OK);
    vn = nullptr;

    ASSERT_TRUE(send_sync(client));

    // A) Wait for sync to begin.
    completion_wait(&completions[0], ZX_TIME_INFINITE);

    client.reset();
    END_HELPER;
}

// Test a case where the VFS object is shut down outside the dispatch loop.
bool test_unposted_teardown() {
    BEGIN_TEST;

     async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    completion_t completions[3];
    fbl::unique_ptr<fs::ManagedVfs> vfs;

    ASSERT_TRUE(sync_start(completions, &loop, &vfs));

    // B) Let sync complete.
    completion_signal(&completions[1]);

    completion_t* vnode_destroyed = &completions[2];
    completion_t shutdown_done;
    vfs->Shutdown([&vnode_destroyed, &shutdown_done](zx_status_t status) {
        ZX_ASSERT(status == ZX_OK);
        // C) Issue an explicit shutdown, check that the Vnode has
        // already torn down.
        ZX_ASSERT(completion_wait(vnode_destroyed, ZX_SEC(0)) == ZX_OK);
        completion_signal(&shutdown_done);
    });
    ASSERT_EQ(completion_wait(&shutdown_done, ZX_SEC(3)), ZX_OK);
    vfs = nullptr;

    END_TEST;
}

// Test a case where the VFS object is shut down as a posted request to the
// dispatch loop.
bool test_posted_teardown() {
    BEGIN_TEST;

     async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    completion_t completions[3];
    fbl::unique_ptr<fs::ManagedVfs> vfs;

    ASSERT_TRUE(sync_start(completions, &loop, &vfs));

    // B) Let sync complete.
    completion_signal(&completions[1]);

    completion_t* vnode_destroyed = &completions[2];
    completion_t shutdown_done;
    ASSERT_EQ(async::PostTask(loop.dispatcher(), [&]() {
        vfs->Shutdown([&vnode_destroyed, &shutdown_done](zx_status_t status) {
            ZX_ASSERT(status == ZX_OK);
            // C) Issue an explicit shutdown, check that the Vnode has
            // already torn down.
            ZX_ASSERT(completion_wait(vnode_destroyed, ZX_SEC(0)) == ZX_OK);
            completion_signal(&shutdown_done);
        });
    }), ZX_OK);
    ASSERT_EQ(completion_wait(&shutdown_done, ZX_SEC(3)), ZX_OK);
    vfs = nullptr;

    END_TEST;
}

// Test a case where the VFS object destroyed inside the callback to Shutdown.
bool test_teardown_delete_this() {
    BEGIN_TEST;

     async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    completion_t completions[3];
    fbl::unique_ptr<fs::ManagedVfs> vfs;

    ASSERT_TRUE(sync_start(completions, &loop, &vfs));

    // B) Let sync complete.
    completion_signal(&completions[1]);

    completion_t* vnode_destroyed = &completions[2];
    completion_t shutdown_done;
    fs::ManagedVfs* raw_vfs = vfs.release();
    raw_vfs->Shutdown([&raw_vfs, &vnode_destroyed, &shutdown_done](zx_status_t status) {
        ZX_ASSERT(status == ZX_OK);
        // C) Issue an explicit shutdown, check that the Vnode has
        // already torn down.
        ZX_ASSERT(completion_wait(vnode_destroyed, ZX_SEC(0)) == ZX_OK);
        delete raw_vfs;
        completion_signal(&shutdown_done);
    });
    ASSERT_EQ(completion_wait(&shutdown_done, ZX_SEC(3)), ZX_OK);

    END_TEST;
}

// Test a case where the VFS object is shut down before a background async
// callback gets the chance to complete.
bool test_teardown_slow_async_callback() {
    BEGIN_TEST;

     async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    completion_t completions[3];
    fbl::unique_ptr<fs::ManagedVfs> vfs;

    ASSERT_TRUE(sync_start(completions, &loop, &vfs));

    completion_t* vnode_destroyed = &completions[2];
    completion_t shutdown_done;
    vfs->Shutdown([&vnode_destroyed, &shutdown_done](zx_status_t status) {
        ZX_ASSERT(status == ZX_OK);
        // C) Issue an explicit shutdown, check that the Vnode has
        // already torn down.
        //
        // Note: Will not be invoked until (B) completes.
        ZX_ASSERT(completion_wait(vnode_destroyed, ZX_SEC(0)) == ZX_OK);
        completion_signal(&shutdown_done);
    });

    // Shutdown should be waiting for our sync to finish.
    ASSERT_EQ(completion_wait(&shutdown_done, ZX_MSEC(10)), ZX_ERR_TIMED_OUT);

    // B) Let sync complete.
    completion_signal(&completions[1]);
    ASSERT_EQ(completion_wait(&shutdown_done, ZX_SEC(3)), ZX_OK);
    vfs = nullptr;

    END_TEST;
}

// Test a case where the VFS object is shut down while a clone request
// is concurrently trying to open a new connection.
bool test_teardown_slow_clone() {
    BEGIN_TEST;

     async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    completion_t completions[3];
    auto vfs = fbl::make_unique<fs::ManagedVfs>(loop.dispatcher());
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    auto vn = fbl::AdoptRef(new AsyncTearDownVnode(completions));
    zx::channel client, server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
    ASSERT_EQ(vn->Open(0, nullptr), ZX_OK);
    ASSERT_EQ(vn->Serve(vfs.get(), fbl::move(server), 0), ZX_OK);
    vn = nullptr;

    // A) Wait for sync to begin.
    // Block the connection to the server in a sync, while simultanously
    // sending a request to open a new connection.
    send_sync(client);
    completion_wait(&completions[0], ZX_TIME_INFINITE);

    zx::channel client2, server2;
    ASSERT_EQ(zx::channel::create(0, &client2, &server2), ZX_OK);
    ASSERT_EQ(fidl_clone_request(client.get(), server2.release(), 0), ZX_OK);

    // The connection is now:
    // - In a sync callback,
    // - Enqueued with a clone request,
    // - Closed.
    client.reset();

    completion_t* vnode_destroyed = &completions[2];
    completion_t shutdown_done;
    vfs->Shutdown([&vnode_destroyed, &shutdown_done](zx_status_t status) {
        ZX_ASSERT(status == ZX_OK);
        // C) Issue an explicit shutdown, check that the Vnode has
        // already torn down.
        //
        // Note: Will not be invoked until (B) completes.
        ZX_ASSERT(completion_wait(vnode_destroyed, ZX_SEC(0)) == ZX_OK);
        completion_signal(&shutdown_done);
    });

    // Shutdown should be waiting for our sync to finish.
    ASSERT_EQ(completion_wait(&shutdown_done, ZX_MSEC(10)), ZX_ERR_TIMED_OUT);

    // B) Let sync complete. This should result in a successful termination
    // of the filesystem, even with the pending clone request.
    completion_signal(&completions[1]);
    ASSERT_EQ(completion_wait(&shutdown_done, ZX_SEC(3)), ZX_OK);
    vfs = nullptr;

    END_TEST;
}

bool test_synchronous_teardown() {
    BEGIN_TEST;
     async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);
    zx::channel client;

    {
        // Tear down the VFS while the async loop is running.
        auto vfs = fbl::make_unique<fs::SynchronousVfs>(loop.dispatcher());
        auto vn = fbl::AdoptRef(new FdCountVnode());
        zx::channel server;
        ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
        ASSERT_EQ(vn->Open(0, nullptr), ZX_OK);
        ASSERT_EQ(vn->Serve(vfs.get(), fbl::move(server), 0), ZX_OK);
    }

    loop.Quit();

    {
        // Tear down the VFS while the async loop is not running.
        auto vfs = fbl::make_unique<fs::SynchronousVfs>(loop.dispatcher());
        auto vn = fbl::AdoptRef(new FdCountVnode());
        zx::channel server;
        ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
        ASSERT_EQ(vn->Open(0, nullptr), ZX_OK);
        ASSERT_EQ(vn->Serve(vfs.get(), fbl::move(server), 0), ZX_OK);
    }

    {
        // Tear down the VFS with no active connections.
        auto vfs = fbl::make_unique<fs::SynchronousVfs>(loop.dispatcher());
    }

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(teardown_tests)
RUN_TEST(test_unposted_teardown)
RUN_TEST(test_posted_teardown)
RUN_TEST(test_teardown_delete_this)
RUN_TEST(test_teardown_slow_async_callback)
RUN_TEST(test_teardown_slow_clone)
RUN_TEST(test_synchronous_teardown)
END_TEST_CASE(teardown_tests)
