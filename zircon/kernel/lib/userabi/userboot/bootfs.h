// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTFS_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTFS_H_

#include <lib/zx/debuglog.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/bootfs.h>
#include <zircon/types.h>

#include <string_view>

class Bootfs {
 public:
  Bootfs(zx::vmar vmar_self, zx::vmo vmo, zx::resource vmex_resource, zx::debuglog log);
  ~Bootfs();

  zx::vmo Open(std::string_view root_prefix, std::string_view filename,
               std::string_view purpose) const;

 private:
  const zbi_bootfs_dirent_t* Search(std::string_view root_prefix, std::string_view filename) const;

  zx::vmar vmar_self_;
  zx::vmo vmo_;
  zx::resource vmex_resource_;
  zx::debuglog log_;
  std::basic_string_view<std::byte> contents_;
};

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BOOTFS_H_
