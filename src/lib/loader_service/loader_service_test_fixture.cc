// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/loader_service/loader_service_test_fixture.h"

#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/zx/channel.h>
#include <lib/zx/object.h>
#include <lib/zx/resource.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>
#include <zircon/limits.h>

#include <string_view>

#define ASSERT_OK(expr) ASSERT_EQ(ZX_OK, expr)
#define EXPECT_OK(expr) EXPECT_EQ(ZX_OK, expr)

namespace loader {
namespace test {

namespace fldsvc = fuchsia_ldsvc;
namespace fkernel = fuchsia_kernel;

namespace {

zx_rights_t get_rights(const zx::object_base& handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.rights : ZX_RIGHT_NONE;
}

}  // namespace

void LoaderServiceTest::TearDown() {
  if (vfs_) {
    std::optional<zx_status_t> shutdown_status;
    vfs_->Shutdown([&](zx_status_t status) { shutdown_status = status; });
    RunLoopUntil([&]() { return shutdown_status.has_value(); });
    ASSERT_OK(shutdown_status.value());
  }
}

void LoaderServiceTest::CreateTestDirectory(std::vector<TestDirectoryEntry> config,
                                            fbl::unique_fd* root_fd) {
  ASSERT_FALSE(vfs_);
  ASSERT_FALSE(root_dir_);

  ASSERT_OK(memfs::Memfs::Create(fs_loop_.dispatcher(), "<tmp>", &vfs_, &root_dir_));

  for (auto entry : config) {
    ASSERT_NO_FATAL_FAILURE(AddDirectoryEntry(root_dir_, entry));
  }

  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));
  ASSERT_OK(vfs_->ServeDirectory(fbl::RefPtr(root_dir_), std::move(server)));

  // Must start fs_loop before fdio_fd_create, since that will attempt to Describe the directory.
  ASSERT_OK(fs_loop_.StartThread("fs_loop"));
  ASSERT_OK(fdio_fd_create(client.release(), root_fd->reset_and_get_address()));

  // The loader needs a separate thread from the FS because it uses synchronous fd-based I/O.
  ASSERT_OK(loader_loop_.StartThread("loader_loop"));
}

void LoaderServiceTest::AddDirectoryEntry(const fbl::RefPtr<memfs::VnodeDir>& root,
                                          TestDirectoryEntry entry) {
  ASSERT_FALSE(entry.path.empty() || entry.path.front() == '/' || entry.path.back() == '/');

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(entry.file_contents.size(), 0, &vmo));
  ASSERT_OK(vmo.write(entry.file_contents.data(), 0, entry.file_contents.size()));
  if (entry.executable) {
    auto vmex_rsrc = GetVmexResource();
    ASSERT_OK(vmex_rsrc.status_value());
    ASSERT_TRUE(vmex_rsrc.value()->is_valid());
    ASSERT_OK(vmo.replace_as_executable(*vmex_rsrc.value(), &vmo));
  }

  fbl::RefPtr<memfs::VnodeDir> dir(root);
  std::string_view view(entry.path);
  while (true) {
    size_t next = view.find('/');
    if (next == std::string_view::npos) {
      // No more subdirectories; create vnode for vmo, then done!
      ASSERT_FALSE(view.empty());
      ASSERT_OK(dir->CreateFromVmo(view, vmo.release(), 0, entry.file_contents.size()));
      return;
    } else {
      // Create subdirectory if it doesn't already exist.
      std::string_view subdir(view.substr(0, next));
      ASSERT_FALSE(subdir.empty());

      fbl::RefPtr<fs::Vnode> out;
      zx_status_t status = dir->Lookup(subdir, &out);
      if (status == ZX_ERR_NOT_FOUND) {
        status = dir->Create(subdir, S_IFDIR, &out);
      }
      ASSERT_OK(status);

      dir = fbl::RefPtr<memfs::VnodeDir>::Downcast(std::move(out));
      view.remove_prefix(next + 1);
    }
  }
}

void LoaderServiceTest::LoadObject(fidl::WireSyncClient<fldsvc::Loader>& client, std::string name,
                                   zx::result<std::string> expected) {
  auto result = client->LoadObject(fidl::StringView::FromExternal(name));
  ASSERT_TRUE(result.ok());
  auto response = result.Unwrap();
  ASSERT_EQ(expected.status_value(), response->rv);

  zx::vmo vmo = std::move(response->object);
  if (expected.is_error()) {
    ASSERT_FALSE(vmo.is_valid());
  } else {
    ASSERT_TRUE(vmo.is_valid());
    ASSERT_EQ(get_rights(vmo) & ZX_RIGHT_EXECUTE, ZX_RIGHT_EXECUTE);

    std::vector<char> data;
    data.resize(expected.value().size() + 1);
    // Null terminator.
    data[data.size() - 1] = 0;
    ASSERT_OK(vmo.read(data.data(), 0, expected.value().size()));
    ASSERT_EQ(std::string(data.data()), expected.value());
  }
}

void LoaderServiceTest::Config(fidl::WireSyncClient<fldsvc::Loader>& client, std::string config,
                               zx::result<zx_status_t> expected) {
  auto result = client->Config(fidl::StringView::FromExternal(config));
  ASSERT_EQ(result.status(), expected.status_value());
  if (expected.is_ok()) {
    ASSERT_EQ(result->rv, expected.value());
  }
}

// static
zx::result<zx::unowned_resource> LoaderServiceTest::GetVmexResource() {
  static zx::resource vmex_resource;
  if (!vmex_resource.is_valid()) {
    zx::channel client, server;
    auto status = zx::make_result(zx::channel::create(0, &client, &server));
    if (status.is_error()) {
      return status.take_error();
    }
    status = zx::make_result(fdio_service_connect(
        fidl::DiscoverableProtocolDefaultPath<fkernel::VmexResource>, server.release()));
    if (status.is_error()) {
      return status.take_error();
    }

    auto result = fidl::WireCall<fkernel::VmexResource>(client.borrow())->Get();
    if (!result.ok()) {
      return zx::error(result.status());
    }
    vmex_resource = std::move(result.value().resource);
  }
  return zx::ok(vmex_resource.borrow());
}

}  // namespace test
}  // namespace loader
