// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/vfs.h>
#include <lib/sync/completion.h>

#include <atomic>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fs/connection.h>
#include <fs/managed-vfs.h>
#include <unittest/unittest.h>

#include "filesystems.h"

namespace {

bool TestConnectionRights() {
  BEGIN_TEST;

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  fbl::unique_ptr<fs::ManagedVfs> vfs = std::make_unique<fs::ManagedVfs>(loop.dispatcher());

  // Set up a vnode for the root directory
  class TestVNode : public fs::Vnode {
   public:
    zx_status_t GetNodeInfo(uint32_t flags, fuchsia_io_NodeInfo* info) {
      info->tag = fuchsia_io_NodeInfoTag_file;
      return ZX_OK;
    }
    bool IsDirectory() const { return false; }
    zx_status_t GetVmo(int flags, zx_handle_t* out_vmo, size_t* out_size) {
      zx::vmo vmo;
      ASSERT_EQ(zx::vmo::create(4096, 0u, &vmo), ZX_OK);
      *out_vmo = vmo.release();
      *out_size = 0;
      return ZX_OK;
    }
  };

  typedef struct test_row {
   public:
    uint32_t connection_flags;    // Or'd ZX_FS_RIGHT_* flags for this connection.
    uint32_t request_flags;       // Or'd fuchsia_io_VMO_FLAG_* values.
    zx_status_t expected_result;  // What we expect FileGetBuffer to return.
  } test_row_t;

  test_row_t test_data[] = {
      // If the connection has all rights, then everything should work.
      {.connection_flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE | ZX_FS_RIGHT_EXECUTABLE,
       .request_flags = fuchsia_io_VMO_FLAG_READ,
       .expected_result = ZX_OK},
      {.connection_flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE | ZX_FS_RIGHT_EXECUTABLE,
       .request_flags = fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_WRITE,
       .expected_result = ZX_OK},
      {.connection_flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE | ZX_FS_RIGHT_EXECUTABLE,
       .request_flags = fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_EXEC,
       .expected_result = ZX_OK},
      // If the connection is missing the EXECUTABLE right, then requests with
      // fuchsia_io_VMO_FLAG_EXEC should fail.
      {.connection_flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE,
       .request_flags = fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_EXEC,
       .expected_result = ZX_ERR_ACCESS_DENIED},

      // If the connection is missing the WRITABLE right, then requests with
      // fuchsia_io_VMO_FLAG_WRITE should fail.
      {.connection_flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_EXECUTABLE,
       .request_flags = fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_WRITE,
       .expected_result = ZX_ERR_ACCESS_DENIED},

  };

  {
    auto vnode = fbl::AdoptRef<TestVNode>(new TestVNode());
    for (test_row_t& row : test_data) {
      // Set up a vfs connection with the testcase's connection flags
      zx::channel client, server;
      ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);
      uint32_t flags = row.connection_flags;
      vnode->Serve(vfs.get(), std::move(server), flags);

      // Call FileGetBuffer on the channel with the testcase's request flags.
      // Check that we get the expected result.
      zx_status_t status;
      fuchsia_mem_Buffer buffer = {};
      EXPECT_EQ(fuchsia_io_FileGetBuffer(client.get(), row.request_flags, &status, &buffer), ZX_OK);

      // Verify that the result matches the value in our test table.
      EXPECT_EQ(status, row.expected_result);
    }
  }

  // Tear down the VFS.  On completion, it will no longer rely on the async
  // loop.  Then, tear down the async loop.
  sync_completion_t completion;
  vfs->Shutdown([&completion](zx_status_t status) {
    EXPECT_EQ(status, ZX_OK);
    sync_completion_signal(&completion);
  });
  sync_completion_wait(&completion, zx::time::infinite().get());
  loop.Shutdown();

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(GetBufferTest)
RUN_TEST(TestConnectionRights)
END_TEST_CASE(GetBufferTest)
