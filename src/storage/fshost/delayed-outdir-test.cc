// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "delayed-outdir.h"

#include <lib/fdio/directory.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/result.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"

namespace fshost {
namespace {

// TODO(fxbug.dev/39588): delete this
TEST(DelayedOutdirTest, MessagesWaitForStart) {
  // Create a new DelayedOutdir, and initialize it with a new channel
  auto delayed_outdir = DelayedOutdir();

  auto delayed = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(delayed.status_value());

  auto remote_dir = delayed_outdir.Initialize(std::move(delayed->client));

  // Put the remote_dir we received from DelayedOutDir in a vfs and run it

  auto root = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(root.status_value());

  auto loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto vfs = fs::ManagedVfs(loop.dispatcher());
  vfs.ServeDirectory(remote_dir, std::move(root->server));
  loop.StartThread("delayed_outgoing_dir_test");

  // Attempt to open "fs/foo" in our vfs, which will forward an open request for
  // "foo" into the channel we provided above.

  auto foo = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(foo.status_value());
  zx_status_t status = fdio_open_at(root->client.channel().get(), "fs/foo", ZX_FS_RIGHT_READABLE,
                                    foo->server.channel().release());
  ASSERT_OK(status);

  // If we attempt to read from the channel behind DelayedOutdir, we should see
  // ZX_ERR_SHOULD_WAIT because the DelayedOutdir isn't running yet, and thus
  // the open we just made hasn't been handled yet.

  uint8_t read_buffer[1024];
  zx_handle_t handle_buffer[16];
  uint32_t actual_bytes = 0;
  uint32_t actual_handles = 0;
  status = delayed->server.channel().read(0, read_buffer, handle_buffer, sizeof(read_buffer),
                                          sizeof(handle_buffer), &actual_bytes, &actual_handles);
  ASSERT_EQ(ZX_ERR_SHOULD_WAIT, status);

  // Now let's start the DelayedOutdir, and wait for the channel to become
  // readable. Once it's readable, that means our open request from above made
  // it through.

  delayed_outdir.Start();
  zx_signals_t observed;
  status = delayed->server.channel().wait_one(ZX_CHANNEL_READABLE, zx::deadline_after(zx::sec(10)),
                                              &observed);
  ASSERT_OK(status);
  ASSERT_TRUE(observed & ZX_CHANNEL_READABLE);

  // Shut down the managed VFS to get it to close active connections, otherwise
  // the deconstructor will crash.

  fpromise::bridge<zx_status_t> bridge;
  vfs.Shutdown(bridge.completer.bind());
  auto promise_shutdown = bridge.consumer.promise_or(::fpromise::error());

  fpromise::result<zx_status_t, void> result =
      fpromise::run_single_threaded(std::move(promise_shutdown));
  ASSERT_TRUE(result.is_ok());
  ASSERT_OK(result.value());
}

}  // namespace
}  // namespace fshost
