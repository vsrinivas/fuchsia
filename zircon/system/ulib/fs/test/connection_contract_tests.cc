// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <utility>

#include <fs/internal/connection.h>
#include <fs/pseudo_dir.h>
#include <sanitizer/lsan_interface.h>
#include <zxtest/zxtest.h>

namespace {

// Base class used to define fake Vfs objects to test |Connection::StartDispatching|.
class NoOpVfs : public fs::Vfs {
 public:
  using Vfs::Vfs;

 protected:
  fbl::DoublyLinkedList<std::unique_ptr<fs::internal::Connection>> connections_;

 private:
  void UnregisterConnection(fs::internal::Connection* connection) final {
    FAIL("Should never be reached in this test");
  }
  void Shutdown(ShutdownCallback handler) override { FAIL("Should never be reached in this test"); }
  bool IsTerminating() const final {
    ADD_FAILURE("Should never be reached in this test");
    return false;
  }
  void CloseAllConnectionsForVnode(const fs::Vnode& node,
                                   CloseAllConnectionsForVnodeCallback callback) final {
    FAIL("Should never be reached in this test");
  }
};

// A Vfs that first places connections into a linked list before
// starting message dispatch.
class NoOpVfsGood : public NoOpVfs {
 public:
  using NoOpVfs::NoOpVfs;

 private:
  zx_status_t RegisterConnection(std::unique_ptr<fs::internal::Connection> connection,
                                 zx::channel server_end) final {
    connections_.push_back(std::move(connection));
    EXPECT_OK(connections_.back().StartDispatching(std::move(server_end)));
    return ZX_OK;
  }
};

// A Vfs that first starts message dispatch on a connection before
// placing it into a linked list. This behavior is racy (fxbug.dev/45912)
// so we test that it triggers a failed precondition check.
class NoOpVfsBad : public NoOpVfs {
 public:
  using NoOpVfs::NoOpVfs;

 private:
  zx_status_t RegisterConnection(std::unique_ptr<fs::internal::Connection> connection,
                                 zx::channel server_end) final {
    EXPECT_OK(connection->StartDispatching(std::move(server_end)));
    connections_.push_back(std::move(connection));
    return ZX_OK;
  }
};

template <typename Vfs>
void RunTest(async::Loop* loop, Vfs&& vfs) {
  auto root = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));

  ASSERT_OK(vfs.Serve(root, std::move(server_end), fs::VnodeConnectionOptions::ReadOnly()));
  loop->RunUntilIdle();
}

TEST(ConnectionTest, StartDispatchingRequiresVfsManagingConnection_Positive) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  RunTest(&loop, NoOpVfsGood(loop.dispatcher()));
}

TEST(ConnectionDeathTest, StartDispatchingRequiresVfsManagingConnection_Negative) {
  // StartDispatching requires registering the connection in a list first.
  if (ZX_DEBUG_ASSERT_IMPLEMENTED) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_DEATH([&] {
#if __has_feature(address_sanitizer) || __has_feature(leak_sanitizer)
      // Disable LSAN, this thread is expected to leak by way of a crash.
      __lsan::ScopedDisabler _;
#endif
      RunTest(&loop, NoOpVfsBad(loop.dispatcher()));
    });
  }
}

}  // namespace
