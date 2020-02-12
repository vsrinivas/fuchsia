// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/memfs/cpp/vnode.h>
#include <lib/memfs/memfs.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <fbl/unique_fd.h>
#include <unittest/unittest.h>

namespace {

namespace fio = ::llcpp::fuchsia::io;

bool test_vmofile_basic() {
  BEGIN_TEST;

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  zx::channel client, server;
  ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);

  std::unique_ptr<memfs::Vfs> vfs;
  fbl::RefPtr<memfs::VnodeDir> root;
  ASSERT_EQ(memfs::Vfs::Create("<tmp>", &vfs, &root), ZX_OK);
  vfs->SetDispatcher(dispatcher);

  zx::vmo backing_vmo;
  ASSERT_EQ(zx::vmo::create(64, 0, &backing_vmo), ZX_OK);
  ASSERT_EQ(backing_vmo.write("hello, world!", 0, 13), ZX_OK);
  ASSERT_EQ(vfs->CreateFromVmo(root.get(), "greeting", backing_vmo.get(), 0, 13), ZX_OK);
  ASSERT_EQ(vfs->ServeDirectory(std::move(root), std::move(server)), ZX_OK);

  zx::channel h, request;
  ASSERT_EQ(zx::channel::create(0, &h, &request), ZX_OK);
  auto open_result =
      fio::Directory::Call::Open(zx::unowned_channel(client), fio::OPEN_RIGHT_READABLE, 0,
                                 fidl::StringView("greeting"), std::move(request));
  ASSERT_EQ(open_result.status(), ZX_OK);

  auto get_result = fio::File::Call::GetBuffer(zx::unowned_channel(h), fio::VMO_FLAG_READ);
  ASSERT_EQ(get_result.status(), ZX_OK);
  ASSERT_EQ(get_result.Unwrap()->s, ZX_OK);
  llcpp::fuchsia::mem::Buffer* buffer = get_result.Unwrap()->buffer;
  ASSERT_TRUE(buffer->vmo.is_valid());
  ASSERT_EQ(buffer->size, 13);

  auto describe_result = fio::File::Call::Describe(zx::unowned_channel(h));
  ASSERT_EQ(describe_result.status(), ZX_OK);
  fio::NodeInfo* info = &describe_result.Unwrap()->info;
  ASSERT_TRUE(info->is_vmofile());
  ASSERT_EQ(info->vmofile().offset, 0u);
  ASSERT_EQ(info->vmofile().length, 13u);

  auto seek_result = fio::File::Call::Seek(zx::unowned_channel(h), 7u, fio::SeekOrigin::START);
  ASSERT_EQ(seek_result.status(), ZX_OK);
  ASSERT_EQ(seek_result.Unwrap()->s, ZX_OK);
  ASSERT_EQ(seek_result.Unwrap()->offset, 7u);

  describe_result = fio::File::Call::Describe(zx::unowned_channel(h));
  ASSERT_EQ(describe_result.status(), ZX_OK);
  info = &describe_result.Unwrap()->info;
  ASSERT_TRUE(info->is_vmofile());
  ASSERT_EQ(info->vmofile().offset, 0u);
  ASSERT_EQ(info->vmofile().length, 13u);

  h.reset();

  sync_completion_t completion;
  vfs->Shutdown([&completion](zx_status_t status) {
    EXPECT_EQ(status, ZX_OK);
    sync_completion_signal(&completion);
  });

  // The following sequence of events must occur to terminate cleanly:
  // 1) Invoke "vfs.Shutdown", passing a closure.
  // 2) Wait for the closure to be invoked, and for |completion| to be signalled. This implies
  // that Shutdown no longer relies on the dispatch loop, nor will it attempt to continue
  // accessing |completion|.
  // 3) Shutdown the dispatch loop.
  //
  // If the dispatch loop is terminated too before the vfs shutdown task completes, it may see
  // "ZX_ERR_CANCELED" posted to the "vfs.Shutdown" closure instead.
  sync_completion_wait(&completion, zx::sec(5).get());
  loop.Shutdown();

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(vmofile_tests)
RUN_TEST(test_vmofile_basic)
END_TEST_CASE(vmofile_tests)
