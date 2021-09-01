// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/vfs.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>

#include <atomic>
#include <memory>

#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

namespace {

namespace fio = fuchsia_io;

TEST(ConnectionRightsTest, RightsBehaveAsExpected) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  std::unique_ptr<fs::ManagedVfs> vfs = std::make_unique<fs::ManagedVfs>(loop.dispatcher());

  // Set up a vnode for the root directory
  class TestVNode : public fs::Vnode {
   public:
    zx_status_t GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                       [[maybe_unused]] fs::Rights rights,
                                       fs::VnodeRepresentation* info) override {
      *info = fs::VnodeRepresentation::File();
      return ZX_OK;
    }
    fs::VnodeProtocolSet GetProtocols() const final { return fs::VnodeProtocol::kFile; }
    zx_status_t GetVmo(int flags, zx::vmo* out_vmo, size_t* out_size) override {
      zx::vmo vmo;
      zx_status_t status = zx::vmo::create(4096, 0u, &vmo);
      EXPECT_EQ(status, ZX_OK);
      if (status != ZX_OK)
        return status;
      *out_vmo = std::move(vmo);
      *out_size = 0;
      return ZX_OK;
    }
  };

  typedef struct test_row {
   public:
    uint32_t connection_flags;    // Or'd ZX_FS_RIGHT_* flags for this connection.
    uint32_t request_flags;       // Or'd fio::wire::kVmoFlag* values.
    zx_status_t expected_result;  // What we expect FileGetBuffer to return.
  } test_row_t;

  test_row_t test_data[] = {
      // If the connection has all rights, then everything should work.
      {.connection_flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE | ZX_FS_RIGHT_EXECUTABLE,
       .request_flags = fio::wire::kVmoFlagRead,
       .expected_result = ZX_OK},
      {.connection_flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE | ZX_FS_RIGHT_EXECUTABLE,
       .request_flags = fio::wire::kVmoFlagRead | fio::wire::kVmoFlagWrite,
       .expected_result = ZX_OK},
      {.connection_flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE | ZX_FS_RIGHT_EXECUTABLE,
       .request_flags = fio::wire::kVmoFlagRead | fio::wire::kVmoFlagExec,
       .expected_result = ZX_OK},
      // If the connection is missing the EXECUTABLE right, then requests with
      // fio::wire::kVmoFlagExec
      // should fail.
      {.connection_flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE,
       .request_flags = fio::wire::kVmoFlagRead | fio::wire::kVmoFlagExec,
       .expected_result = ZX_ERR_ACCESS_DENIED},

      // If the connection is missing the WRITABLE right, then requests with
      // fio::wire::kVmoFlagWrite
      // should fail.
      {.connection_flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_EXECUTABLE,
       .request_flags = fio::wire::kVmoFlagRead | fio::wire::kVmoFlagWrite,
       .expected_result = ZX_ERR_ACCESS_DENIED},

  };

  {
    auto vnode = fbl::AdoptRef<TestVNode>(new TestVNode());
    for (test_row_t& row : test_data) {
      // Set up a vfs connection with the testcase's connection flags
      zx::channel client, server;
      ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
      uint32_t flags = row.connection_flags;
      vfs->Serve(vnode, std::move(server), fs::VnodeConnectionOptions::FromIoV1Flags(flags));

      // Call FileGetBuffer on the channel with the testcase's request flags. Check that we get the
      // expected result.
      auto result =
          fidl::WireCall<fio::File>(zx::unowned_channel(client.get())).GetBuffer(row.request_flags);
      EXPECT_EQ(result.status(), ZX_OK);

      // Verify that the result matches the value in our test table.
      EXPECT_EQ(result.Unwrap()->s, row.expected_result);
    }
  }

  // Tear down the VFS. On completion, it will no longer rely on the async loop. Then, tear down the
  // async loop.
  sync_completion_t completion;
  vfs->Shutdown([&completion](zx_status_t status) {
    EXPECT_EQ(status, ZX_OK);
    sync_completion_signal(&completion);
  });
  sync_completion_wait(&completion, zx::time::infinite().get());
  loop.Shutdown();
}

}  // namespace
