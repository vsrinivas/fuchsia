// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zbi-test-entry.h"

#include <fcntl.h>
#include <lib/standalone-test/standalone.h>
#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/items/bootfs.h>
#include <lib/zbitl/view.h>
#include <lib/zbitl/vmo.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <zircon/status.h>
#include <zircon/syscalls/resource.h>

#include <cstddef>
#include <string_view>

#include <src/bringup/lib/mexec/mexec.h>

constexpr const char* kMexecZbi = "testdata/mexec-child.zbi";

namespace {

// The bootfs VFS (rooted under '/boot') is hosted by component manager. These tests can be started
// directly from userboot without starting component manager, so the bootfs VFS will not be
// available. Instead, we can just read any files needed directly from the uncompressed bootfs VMO.
zx_status_t GetFileFromBootfs(std::string_view path, zbitl::MapUnownedVmo bootfs, zx::vmo* vmo) {
  using Bootfs = zbitl::Bootfs<zbitl::MapUnownedVmo>;

  Bootfs reader;
  if (auto result = Bootfs::Create(std::move(bootfs)); result.is_error()) {
    zbitl::PrintBootfsError(result.error_value());
    return ZX_ERR_INTERNAL;
  } else {
    reader = std::move(result).value();
  }

  auto view = reader.root();
  auto file = view.find(path);
  if (auto result = view.take_error(); result.is_error()) {
    zbitl::PrintBootfsError(result.error_value());
    return ZX_ERR_INTERNAL;
  }
  if (file == view.end()) {
    return ZX_ERR_NOT_FOUND;
  }

  return reader.storage().vmo().create_child(ZX_VMO_CHILD_SNAPSHOT | ZX_VMO_CHILD_NO_WRITE,
                                             file->offset, file->size, vmo);
}

}  // namespace

zx::status<> ZbiTestEntry::Init(int argc, char** argv) {
  ZX_ASSERT(argc > 0);
  const char* program_name = argv[0];

  zx::unowned_vmo bootfs(standalone::GetVmo("uncompressed-bootfs"));
  if (!bootfs->is_valid()) {
    printf("%s: received an invalid bootfs VMO handle\n", program_name);
    return zx::error(ZX_ERR_INTERNAL);
  }

  {
    zx::vmo vmo;
    zbitl::MapUnownedVmo unowned_bootfs(bootfs->borrow());
    if (zx_status_t status = GetFileFromBootfs(kMexecZbi, unowned_bootfs, &vmo); status != ZX_OK) {
      printf("%s: failed to get child ZBI's VMO: %s\n", program_name, zx_status_get_string(status));
      return zx::error(status);
    }

    zbitl::View view(std::move(vmo));
    if (view.begin() == view.end()) {
      if (auto result = view.take_error(); result.is_error()) {
        printf("%s: invalid child ZBI: ", program_name);
        zbitl::PrintViewError(result.error_value());
      } else {
        printf("%s: empty child ZBI\n", program_name);
      }
      return zx::error(ZX_ERR_INTERNAL);
    }

    auto first = view.begin();
    auto second = std::next(first);
    if (auto result = view.Copy(first, second); result.is_error()) {
      printf("%s: failed to copy out kernel payload: ", program_name);
      zbitl::PrintViewCopyError(result.error_value());
      view.ignore_error();
      return zx::error(ZX_ERR_INTERNAL);
    } else {
      kernel_zbi_ = std::move(result).value();
    }

    // Ensure that the data ZBI is resizable, as it wil be extended in
    // `mexec::PrepareDataZbi()` below.
    if (zx_status_t status = zx::vmo::create(0u, ZX_VMO_RESIZABLE, &data_zbi_); status != ZX_OK) {
      return zx::error{status};
    }
    if (auto result = view.Copy(data_zbi_, second, view.end()); result.is_error()) {
      printf("%s: failed to copy out data ZBI: ", program_name);
      zbitl::PrintViewCopyError(result.error_value());
      view.ignore_error();
      return zx::error(ZX_ERR_INTERNAL);
    }

    if (auto result = view.take_error(); result.is_error()) {
      printf("%s: ZBI iteration failure: ", program_name);
      zbitl::PrintViewError(result.error_value());
      return zx::error(ZX_ERR_INTERNAL);
    }
  }

  zx_status_t status =
      zx::resource::create(*standalone::GetSystemRootResource(), ZX_RSRC_KIND_SYSTEM,
                           ZX_RSRC_SYSTEM_MEXEC_BASE, 1, nullptr, 0, &mexec_resource_);
  if (status != ZX_OK || !mexec_resource_.is_valid()) {
    printf("%s: unable to get ahold of the mexec resource\n", program_name);
    return zx::error(ZX_ERR_INTERNAL);
  }

  status = mexec::PrepareDataZbi(mexec_resource_.borrow(), data_zbi_.borrow());
  if (status != ZX_OK) {
    printf("%s: failed to prepare data ZBI: %s\n", program_name, zx_status_get_string(status));
    return zx::error(status);
  }

  return zx::ok();
}
