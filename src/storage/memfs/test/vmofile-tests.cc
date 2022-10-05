// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/memfs/memfs.h>
#include <lib/sys/component/cpp/service_client.h>
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

#include "src/storage/memfs/memfs.h"
#include "src/storage/memfs/vnode_dir.h"

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
void shutdown_vfs(std::unique_ptr<memfs::Memfs> vfs) {
  sync_completion_t completion;
  vfs->Shutdown([&completion](zx_status_t status) {
    EXPECT_OK(status);
    sync_completion_signal(&completion);
  });
  sync_completion_wait(&completion, zx::sec(5).get());
}

TEST(VmofileTests, test_vmofile_basic) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  async_dispatcher_t* dispatcher = loop.dispatcher();

  zx::status directory_endpoints = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(directory_endpoints.status_value());

  std::unique_ptr<memfs::Memfs> vfs;
  fbl::RefPtr<memfs::VnodeDir> root;
  ASSERT_OK(memfs::Memfs::Create(dispatcher, "<tmp>", &vfs, &root));

  zx::vmo read_only_vmo;
  ASSERT_OK(zx::vmo::create(64, 0, &read_only_vmo));
  ASSERT_OK(read_only_vmo.write("hello, world!", 0, 13));
  ASSERT_OK(vfs->CreateFromVmo(root.get(), "greeting", read_only_vmo.get(), 0, 13));
  ASSERT_OK(vfs->ServeDirectory(std::move(root), std::move(directory_endpoints->server)));

  zx::status node_endpoints = fidl::CreateEndpoints<fio::Node>();
  ASSERT_OK(node_endpoints.status_value());
  auto open_result = fidl::WireCall(directory_endpoints->client)
                         ->Open(fio::wire::OpenFlags::kRightReadable, 0,
                                fidl::StringView("greeting"), std::move(node_endpoints->server));
  ASSERT_OK(open_result.status());
  fidl::ClientEnd<fio::File> file(node_endpoints->client.TakeChannel());

  {
    const fidl::WireResult get_result =
        fidl::WireCall(file)->GetBackingMemory(fio::wire::VmoFlags::kRead);
    ASSERT_TRUE(get_result.ok(), "%s", get_result.FormatDescription().c_str());
    const auto& get_response = get_result.value();
    ASSERT_TRUE(get_response.is_ok(), "%s", zx_status_get_string(get_response.error_value()));
    const zx::vmo& vmo = get_response.value()->vmo;
    ASSERT_TRUE(vmo.is_valid());
    ASSERT_EQ(get_rights(vmo), kCommonExpectedRights);
    uint64_t size;
    ASSERT_OK(vmo.get_prop_content_size(&size));
    ASSERT_EQ(size, 13);
  }

  {
    const fidl::WireResult get_result = fidl::WireCall(file)->GetBackingMemory(
        fio::wire::VmoFlags::kRead | fio::wire::VmoFlags::kPrivateClone);
    ASSERT_TRUE(get_result.ok(), "%s", get_result.FormatDescription().c_str());
    const auto& get_response = get_result.value();
    ASSERT_TRUE(get_response.is_ok(), "%s", zx_status_get_string(get_response.error_value()));
    const zx::vmo& vmo = get_response.value()->vmo;
    ASSERT_TRUE(vmo.is_valid());
    ASSERT_EQ(get_rights(vmo), kCommonExpectedRights | ZX_RIGHT_SET_PROPERTY);
    uint64_t size;
    ASSERT_OK(vmo.get_prop_content_size(&size));
    ASSERT_EQ(size, 13);
  }

  {
    const fidl::WireResult get_result = fidl::WireCall(file)->GetBackingMemory(
        fio::wire::VmoFlags::kRead | fio::wire::VmoFlags::kExecute);
    ASSERT_TRUE(get_result.ok(), "%s", get_result.FormatDescription().c_str());
    const auto& get_response = get_result.value();
    ASSERT_TRUE(get_response.is_error());
    ASSERT_STATUS(get_response.error_value(), ZX_ERR_ACCESS_DENIED);
  }

  {
    const fidl::WireResult get_result = fidl::WireCall(file)->GetBackingMemory(
        fio::wire::VmoFlags::kRead | fio::wire::VmoFlags::kWrite);
    ASSERT_TRUE(get_result.ok(), "%s", get_result.FormatDescription().c_str());
    const auto& get_response = get_result.value();
    ASSERT_TRUE(get_response.is_error());
    ASSERT_STATUS(get_response.error_value(), ZX_ERR_ACCESS_DENIED);
  }

  {
    auto describe_result = fidl::WireCall(file)->DescribeDeprecated();
    ASSERT_OK(describe_result.status());
    fio::wire::NodeInfoDeprecated* info = &describe_result->info;
    ASSERT_TRUE(info->is_file());
  }

  {
    const fidl::WireResult seek_result =
        fidl::WireCall(file)->Seek(fio::wire::SeekOrigin::kStart, 7u);
    ASSERT_TRUE(seek_result.ok(), "%s", seek_result.status_string());
    const fit::result response = seek_result.value();
    ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
    ASSERT_EQ(response.value()->offset_from_start, 7u);
  }

  shutdown_vfs(std::move(vfs));
}

TEST(VmofileTests, test_vmofile_exec) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  async_dispatcher_t* dispatcher = loop.dispatcher();

  zx::status directory_endpoints = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(directory_endpoints.status_value());

  std::unique_ptr<memfs::Memfs> vfs;
  fbl::RefPtr<memfs::VnodeDir> root;
  ASSERT_OK(memfs::Memfs::Create(dispatcher, "<tmp>", &vfs, &root));

  zx::vmo read_exec_vmo;
  ASSERT_OK(zx::vmo::create(64, 0, &read_exec_vmo));
  ASSERT_OK(read_exec_vmo.write("hello, world!", 0, 13));

  zx::status client_end = component::Connect<fuchsia_kernel::VmexResource>();
  ASSERT_OK(client_end.status_value());
  fidl::WireResult result = fidl::WireCall(client_end.value())->Get();
  ASSERT_TRUE(result.ok(), "%s", result.FormatDescription().c_str());
  auto& response = result.value();

  ASSERT_OK(read_exec_vmo.replace_as_executable(response.resource, &read_exec_vmo));
  ASSERT_OK(vfs->CreateFromVmo(root.get(), "read_exec", read_exec_vmo.get(), 0, 13));
  ASSERT_OK(vfs->ServeDirectory(std::move(root), std::move(directory_endpoints->server)));

  zx::status node_endpoints = fidl::CreateEndpoints<fio::Node>();
  ASSERT_OK(node_endpoints.status_value());
  auto open_result =
      fidl::WireCall(directory_endpoints->client)
          ->Open(fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightExecutable, 0,
                 fidl::StringView("read_exec"), std::move(node_endpoints->server));
  ASSERT_OK(open_result.status());
  fidl::ClientEnd<fio::File> file(node_endpoints->client.TakeChannel());

  {
    const fidl::WireResult get_result =
        fidl::WireCall(file)->GetBackingMemory(fio::wire::VmoFlags::kRead);
    ASSERT_TRUE(get_result.ok(), "%s", get_result.FormatDescription().c_str());
    const auto& get_response = get_result.value();
    ASSERT_TRUE(get_response.is_ok(), "%s", zx_status_get_string(get_response.error_value()));
    const zx::vmo& vmo = get_response.value()->vmo;
    ASSERT_TRUE(vmo.is_valid());
    ASSERT_EQ(get_rights(vmo), kCommonExpectedRights);
    uint64_t size;
    ASSERT_OK(vmo.get_prop_content_size(&size));
    ASSERT_EQ(size, 13);
  }

  {
    // Providing a backing VMO with ZX_RIGHT_EXECUTE in CreateFromVmo above should cause
    // VmoFlags::EXECUTE to work.
    const fidl::WireResult get_result = fidl::WireCall(file)->GetBackingMemory(
        fio::wire::VmoFlags::kRead | fio::wire::VmoFlags::kExecute);
    ASSERT_TRUE(get_result.ok(), "%s", get_result.FormatDescription().c_str());
    const auto& get_response = get_result.value();
    ASSERT_TRUE(get_response.is_ok(), "%s", zx_status_get_string(get_response.error_value()));
    const zx::vmo& vmo = get_response.value()->vmo;
    ASSERT_TRUE(vmo.is_valid());
    ASSERT_EQ(get_rights(vmo), kCommonExpectedRights | ZX_RIGHT_EXECUTE);
    uint64_t size;
    ASSERT_OK(vmo.get_prop_content_size(&size));
    ASSERT_EQ(size, 13);
  }

  {
    auto describe_result = fidl::WireCall(file)->DescribeDeprecated();
    ASSERT_OK(describe_result.status());
    fio::wire::NodeInfoDeprecated* info = &describe_result->info;
    ASSERT_TRUE(info->is_file());
  }

  shutdown_vfs(std::move(vfs));
}

}  // namespace
