// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/bootsvc/bootfs-loader-service.h"

#include <lib/fit/defer.h>
#include <lib/zx/vmar.h>
#include <zircon/boot/bootfs.h>
#include <zircon/errors.h>

#include <cstddef>

#include <gtest/gtest.h>

#include "src/lib/loader_service/loader_service_test_fixture.h"

#define ASSERT_OK(expr) ASSERT_EQ(ZX_OK, expr)
#define EXPECT_OK(expr) EXPECT_EQ(ZX_OK, expr)

namespace bootsvc {
namespace {

using namespace loader::test;

namespace fldsvc = ::llcpp::fuchsia::ldsvc;

struct BootfsDirectoryEntry {
  std::string path;
  std::string file_contents;
};

static zx::status<zx::vmo> GenerateBootfs(std::vector<BootfsDirectoryEntry> config) {
  // Simplified VMO size calculation assuming each file's contents is no larger than a page (checked
  // below) and dirents are max size.
  uint64_t data_start =
      ZBI_BOOTFS_PAGE_ALIGN(sizeof(zbi_bootfs_header_t) +
                            config.size() * ZBI_BOOTFS_DIRENT_SIZE(ZBI_BOOTFS_MAX_NAME_LEN));
  uint64_t vmo_size = data_start + config.size() * ZBI_BOOTFS_PAGE_SIZE;

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(vmo_size, 0, &vmo);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  uintptr_t mapped = 0;
  status =
      zx::vmar::root_self()->map(0, vmo, 0, vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &mapped);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  auto unmap = fit::defer([=] { zx::vmar::root_self()->unmap(mapped, vmo_size); });

  // Write directory entries and data.
  uintptr_t dirent_ptr = mapped + sizeof(zbi_bootfs_header_t);
  uintptr_t data_ptr = mapped + data_start;
  for (auto entry : config) {
    auto dirent = reinterpret_cast<zbi_bootfs_dirent_t*>(dirent_ptr);
    char* data = reinterpret_cast<char*>(data_ptr);

    dirent->name_len = entry.path.size() + 1;
    dirent->data_len = entry.file_contents.size() + 1;
    if (dirent->data_len > ZBI_BOOTFS_PAGE_SIZE) {
      // Check assumption made above when sizing VMO.
      return zx::error(ZX_ERR_INTERNAL);
    }
    dirent->data_off = data_ptr - mapped;

    memcpy(dirent->name, entry.path.c_str(), dirent->name_len);
    memcpy(data, entry.file_contents.c_str(), dirent->data_len);

    dirent_ptr += ZBI_BOOTFS_DIRENT_SIZE(dirent->name_len);
    data_ptr += ZBI_BOOTFS_PAGE_SIZE;
  }

  // Write main header now that we know exact size of dirents.
  auto hdr = reinterpret_cast<zbi_bootfs_header_t*>(mapped);
  hdr->magic = ZBI_BOOTFS_MAGIC;
  hdr->dirsize = dirent_ptr - mapped - sizeof(zbi_bootfs_header_t);

  return zx::ok(std::move(vmo));
}

class BootfsLoaderServiceTest : public LoaderServiceTest {
 public:
  void CreateTestLoader(std::vector<BootfsDirectoryEntry> config,
                        std::shared_ptr<BootfsLoaderService>* loader) {
    zx::resource vmex;
    zx::status<zx::unowned_resource> unowned_vmex = GetVmexResource();
    ASSERT_OK(unowned_vmex.status_value());
    ASSERT_TRUE(unowned_vmex.value()->is_valid());
    ASSERT_OK(unowned_vmex.value()->duplicate(ZX_RIGHT_SAME_RIGHTS, &vmex));

    fbl::RefPtr<BootfsService> bootfs_svc;
    ASSERT_OK(BootfsService::Create(fs_loop().dispatcher(), std::move(vmex), &bootfs_svc));

    auto bootfs_vmo = GenerateBootfs(std::move(config));
    ASSERT_OK(bootfs_vmo.status_value());
    ASSERT_OK(bootfs_svc->AddBootfs(std::move(bootfs_vmo).value()));

    *loader = BootfsLoaderService::Create(loader_loop().dispatcher(), std::move(bootfs_svc));

    ASSERT_OK(fs_loop().StartThread("fs_loop"));
    ASSERT_OK(loader_loop().StartThread("loader_loop"));
  }
};

TEST_F(BootfsLoaderServiceTest, LoadObject) {
  std::shared_ptr<BootfsLoaderService> loader;
  std::vector<BootfsDirectoryEntry> config = {
      {"lib/libfoo.so", "foo"},
      {"lib/asan/libfoo.so", "asan foo"},
  };
  ASSERT_NO_FATAL_FAILURE(CreateTestLoader(std::move(config), &loader));

  auto status = loader->Connect();
  ASSERT_TRUE(status.is_ok());
  fldsvc::Loader::SyncClient client(std::move(status.value()));

  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "missing", zx::error(ZX_ERR_NOT_FOUND)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("foo")));
  ASSERT_NO_FATAL_FAILURE(Config(client, "asan", zx::ok(ZX_OK)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("asan foo")));
}

}  // namespace
}  // namespace bootsvc
