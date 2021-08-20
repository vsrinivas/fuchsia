// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTFS_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTFS_H_

#include <lib/zbitl/items/bootfs.h>
#include <lib/zbitl/vmo.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/bootfs.h>
#include <zircon/types.h>

#include <string_view>

class Bootfs {
 public:
  Bootfs(zx::unowned_vmar vmar_self, zx::vmo vmo, zx::resource vmex_resource, zx::debuglog log)
      : vmex_resource_(std::move(vmex_resource)), log_(std::move(log)) {
    zbitl::MapOwnedVmo mapvmo{std::move(vmo), /*writable=*/false, std::move(vmar_self)};
    if (auto result = BootfsView::Create(std::move(mapvmo)); result.is_error()) {
      Fail(result.error_value());
    } else {
      bootfs_ = std::move(result.value());
    }
  }

  zx::vmo Open(std::string_view root, std::string_view filename, std::string_view purpose);

 private:
  using BootfsView = zbitl::BootfsView<zbitl::MapOwnedVmo>;

  [[noreturn]] void Fail(const BootfsView::Error& error);

  // It will be asserted on destruction that bootfs_ does not hold an error. We
  // take it as an invariant that bootfs_ never holds an error outside of calls
  // to Open().
  BootfsView bootfs_;
  zx::resource vmex_resource_;
  zx::debuglog log_;
};

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTFS_H_
