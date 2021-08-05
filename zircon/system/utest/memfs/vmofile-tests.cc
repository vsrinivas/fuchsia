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
#include <zircon/errors.h>
#include <zircon/processargs.h>
#include <zircon/rights.h>
#include <zircon/syscalls.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

namespace fio = fuchsia_io;

// These are rights that are common to the various rights checks below.
const uint32_t kCommonExpectedRights =
    ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_READ | ZX_RIGHT_GET_PROPERTY;

zx_rights_t get_rights(const zx::object_base& handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.rights : ZX_RIGHT_NONE;
}

// The following sequence of events must occur to terminate cleanly:
// 1) Invoke "vfs.Shutdown", passing a closure.
// 2) Wait for the closure to be invoked, and for |completion| to be signalled. This implies
// that Shutdown no longer relies on the dispatch loop, nor will it attempt to continue
// accessing |completion|.
// 3) Shutdown the dispatch loop (happens automatically when the async::Loop goes out of scope).
//
// If the dispatch loop is terminated too before the vfs shutdown task completes, it may see
// "ZX_ERR_CANCELED" posted to the "vfs.Shutdown" closure instead.
void shutdown_vfs(std::unique_ptr<memfs::Vfs> vfs) {
  sync_completion_t completion;
  vfs->Shutdown([&completion](zx_status_t status) {
    EXPECT_EQ(status, ZX_OK);
    sync_completion_signal(&completion);
  });
  sync_completion_wait(&completion, zx::sec(5).get());
}

TEST(VmofileTests, test_vmofile_basic) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  zx::channel client, server;
  ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);

  std::unique_ptr<memfs::Vfs> vfs;
  fbl::RefPtr<memfs::VnodeDir> root;
  ASSERT_EQ(memfs::Vfs::Create(dispatcher, "<tmp>", &vfs, &root), ZX_OK);

  zx::vmo read_only_vmo;
  ASSERT_EQ(zx::vmo::create(64, 0, &read_only_vmo), ZX_OK);
  ASSERT_EQ(read_only_vmo.write("hello, world!", 0, 13), ZX_OK);
  ASSERT_EQ(vfs->CreateFromVmo(root.get(), "greeting", read_only_vmo.get(), 0, 13), ZX_OK);
  ASSERT_EQ(vfs->ServeDirectory(std::move(root), std::move(server)), ZX_OK);

  zx::channel h, request;
  ASSERT_EQ(zx::channel::create(0, &h, &request), ZX_OK);
  auto open_result =
      fidl::WireCall<fio::Directory>(zx::unowned_channel(client))
          .Open(fio::wire::kOpenRightReadable, 0, fidl::StringView("greeting"), std::move(request));
  ASSERT_EQ(open_result.status(), ZX_OK);

  {
    auto get_result =
        fidl::WireCall<fio::File>(zx::unowned_channel(h)).GetBuffer(fio::wire::kVmoFlagRead);
    ASSERT_EQ(get_result.status(), ZX_OK);
    ASSERT_EQ(get_result.Unwrap()->s, ZX_OK);
    fuchsia_mem::wire::Buffer* buffer = get_result.Unwrap()->buffer.get();
    ASSERT_TRUE(buffer->vmo.is_valid());
    ASSERT_EQ(get_rights(buffer->vmo), kCommonExpectedRights);
    ASSERT_EQ(buffer->size, 13);
  }

  {
    auto get_result = fidl::WireCall<fio::File>(zx::unowned_channel(h))
                          .GetBuffer(fio::wire::kVmoFlagRead | fio::wire::kVmoFlagPrivate);
    ASSERT_EQ(get_result.status(), ZX_OK);
    ASSERT_EQ(get_result.Unwrap()->s, ZX_OK);
    fuchsia_mem::wire::Buffer* buffer = get_result.Unwrap()->buffer.get();
    ASSERT_TRUE(buffer->vmo.is_valid());
    ASSERT_EQ(get_rights(buffer->vmo), kCommonExpectedRights | ZX_RIGHT_SET_PROPERTY);
    ASSERT_EQ(buffer->size, 13);
  }

  {
    auto get_result = fidl::WireCall<fio::File>(zx::unowned_channel(h))
                          .GetBuffer(fio::wire::kVmoFlagRead | fio::wire::kVmoFlagExec);
    ASSERT_EQ(get_result.status(), ZX_OK);
    ASSERT_EQ(get_result.Unwrap()->s, ZX_ERR_ACCESS_DENIED);
    ASSERT_EQ(get_result.Unwrap()->buffer.get(), nullptr);
  }

  {
    auto get_result = fidl::WireCall<fio::File>(zx::unowned_channel(h))
                          .GetBuffer(fio::wire::kVmoFlagRead | fio::wire::kVmoFlagWrite);
    ASSERT_EQ(get_result.status(), ZX_OK);
    ASSERT_EQ(get_result.Unwrap()->s, ZX_ERR_ACCESS_DENIED);
    ASSERT_EQ(get_result.Unwrap()->buffer.get(), nullptr);
  }

  {
    auto describe_result = fidl::WireCall<fio::File>(zx::unowned_channel(h)).Describe();
    ASSERT_EQ(describe_result.status(), ZX_OK);
    fio::wire::NodeInfo* info = &describe_result.Unwrap()->info;
    ASSERT_TRUE(info->is_vmofile());
    ASSERT_EQ(info->vmofile().offset, 0u);
    ASSERT_EQ(info->vmofile().length, 13u);
    ASSERT_TRUE(info->vmofile().vmo.is_valid());
    ASSERT_EQ(get_rights(info->vmofile().vmo), kCommonExpectedRights);
  }

  {
    auto seek_result =
        fidl::WireCall<fio::File>(zx::unowned_channel(h)).Seek(7u, fio::wire::SeekOrigin::kStart);
    ASSERT_EQ(seek_result.status(), ZX_OK);
    ASSERT_EQ(seek_result.Unwrap()->s, ZX_OK);
    ASSERT_EQ(seek_result.Unwrap()->offset, 7u);
  }

  shutdown_vfs(std::move(vfs));
}

TEST(VmofileTests, test_vmofile_exec) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  zx::channel client, server;
  ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);

  std::unique_ptr<memfs::Vfs> vfs;
  fbl::RefPtr<memfs::VnodeDir> root;
  ASSERT_EQ(memfs::Vfs::Create(dispatcher, "<tmp>", &vfs, &root), ZX_OK);

  zx::vmo read_exec_vmo;
  ASSERT_EQ(zx::vmo::create(64, 0, &read_exec_vmo), ZX_OK);
  ASSERT_EQ(read_exec_vmo.write("hello, world!", 0, 13), ZX_OK);
  // TODO: Update this test to a VMEX resource from fuchsia.security.resource.Vmex instead of
  // relying on the VMEX job policy
  ASSERT_EQ(read_exec_vmo.replace_as_executable(zx::resource(), &read_exec_vmo), ZX_OK);
  ASSERT_EQ(vfs->CreateFromVmo(root.get(), "read_exec", read_exec_vmo.get(), 0, 13), ZX_OK);
  ASSERT_EQ(vfs->ServeDirectory(std::move(root), std::move(server)), ZX_OK);

  zx::channel h, request;
  ASSERT_EQ(zx::channel::create(0, &h, &request), ZX_OK);
  auto open_result = fidl::WireCall<fio::Directory>(zx::unowned_channel(client))
                         .Open(fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable, 0,
                               fidl::StringView("read_exec"), std::move(request));
  ASSERT_EQ(open_result.status(), ZX_OK);

  {
    auto get_result =
        fidl::WireCall<fio::File>(zx::unowned_channel(h)).GetBuffer(fio::wire::kVmoFlagRead);
    ASSERT_EQ(get_result.status(), ZX_OK);
    ASSERT_EQ(get_result.Unwrap()->s, ZX_OK);
    fuchsia_mem::wire::Buffer* buffer = get_result.Unwrap()->buffer.get();
    ASSERT_TRUE(buffer->vmo.is_valid());
    ASSERT_EQ(get_rights(buffer->vmo), kCommonExpectedRights);
    ASSERT_EQ(buffer->size, 13);
  }

  {
    // Providing a backing VMO with ZX_RIGHT_EXECUTE in CreateFromVmo above should cause
    // VMO_FLAG_EXEC to work.
    auto get_result = fidl::WireCall<fio::File>(zx::unowned_channel(h))
                          .GetBuffer(fio::wire::kVmoFlagRead | fio::wire::kVmoFlagExec);
    ASSERT_EQ(get_result.status(), ZX_OK);
    ASSERT_EQ(get_result.Unwrap()->s, ZX_OK);
    auto buffer = get_result.Unwrap()->buffer.get();
    ASSERT_TRUE(buffer->vmo.is_valid());
    ASSERT_EQ(get_rights(buffer->vmo), kCommonExpectedRights | ZX_RIGHT_EXECUTE);
    ASSERT_EQ(buffer->size, 13);
  }

  {
    // Describe should also return a VMO with ZX_RIGHT_EXECUTE.
    auto describe_result = fidl::WireCall<fio::File>(zx::unowned_channel(h)).Describe();
    ASSERT_EQ(describe_result.status(), ZX_OK);
    fio::wire::NodeInfo* info = &describe_result.Unwrap()->info;
    ASSERT_TRUE(info->is_vmofile());
    ASSERT_EQ(info->vmofile().offset, 0u);
    ASSERT_EQ(info->vmofile().length, 13u);
    ASSERT_TRUE(info->vmofile().vmo.is_valid());
    ASSERT_EQ(get_rights(info->vmofile().vmo), kCommonExpectedRights | ZX_RIGHT_EXECUTE);
  }

  shutdown_vfs(std::move(vfs));
}

}  // namespace
