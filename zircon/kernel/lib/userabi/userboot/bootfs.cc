// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "bootfs.h"

#include <lib/stdcompat/string_view.h>
#include <string.h>
#include <zircon/boot/bootfs.h>
#include <zircon/syscalls.h>

#include "util.h"
#include "zircon/rights.h"
#include "zircon/types.h"

Bootfs::Bootfs(zx::vmar vmar_self, zx::vmo vmo, zx::resource vmex_resource, zx::debuglog log)
    : vmar_self_(std::move(vmar_self)),
      vmex_resource_(std::move(vmex_resource)),
      log_(std::move(log)) {
  zx_status_t status = vmo.replace(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP, &vmo_);
  check(log_, status, "zx_handle_replace failed on bootfs VMO handle");

  size_t size;
  status = vmo_.get_size(&size);
  check(log_, status, "zx_vmo_get_size failed on bootfs vmo");

  uintptr_t addr = 0;
  status = vmar_self_.map(ZX_VM_PERM_READ, 0, vmo_, 0, size, &addr);
  check(log_, status, "zx_vmar_map failed on bootfs vmo");
  contents_ = std::basic_string_view(reinterpret_cast<const std::byte*>(addr), size);
}

Bootfs::~Bootfs() {
  uintptr_t addr = reinterpret_cast<uintptr_t>(contents_.data());
  zx_status_t status = vmar_self_.unmap(addr, contents_.size());
  check(log_, status, "zx_vmar_unmap failed on bootfs mapping");
}

const zbi_bootfs_dirent_t* Bootfs::Search(std::string_view root_prefix,
                                          std::string_view filename) const {
  std::basic_string_view<std::byte> p = contents_;

  if (p.size() < sizeof(zbi_bootfs_header_t)) {
    fail(log_, "bootfs is too small");
  }

  const zbi_bootfs_header_t* hdr = reinterpret_cast<const zbi_bootfs_header_t*>(p.data());
  if ((hdr->magic != ZBI_BOOTFS_MAGIC) || (hdr->dirsize > p.size())) {
    fail(log_, "bootfs bad magic or size");
  }

  p = p.substr(sizeof(zbi_bootfs_header_t));
  while (p.size() > sizeof(zbi_bootfs_dirent_t)) {
    auto e = reinterpret_cast<const zbi_bootfs_dirent_t*>(p.data());

    size_t sz = ZBI_BOOTFS_DIRENT_SIZE(e->name_len);
    if ((e->name_len < 1) || (sz > p.size())) {
      fail(log_, "bootfs has bogus namelen in header");
    }

    std::string_view name{e->name, e->name_len - 1};  // Truncate the NUL terminator.
    if (name.size() == root_prefix.size() + filename.size() &&
        cpp20::starts_with(name, root_prefix) && cpp20::ends_with(name, filename)) {
      return e;
    }

    p = p.substr(sz);
  }

  return NULL;
}

zx::vmo Bootfs::Open(std::string_view root_prefix, std::string_view filename,
                     std::string_view purpose) const {
  printl(log_, "searching bootfs for '%.*s%.*s' (%.*s)",            //
         static_cast<int>(root_prefix.size()), root_prefix.data(),  //
         static_cast<int>(filename.size()), filename.data(),        //
         static_cast<int>(purpose.size()), purpose.data());

  const zbi_bootfs_dirent_t* e = Search(root_prefix, filename);
  if (e == NULL) {
    printl(log_, "file not found");
    return zx::vmo{};
  }
  if (e->data_off > contents_.size()) {
    fail(log_, "bogus offset in bootfs header!");
  }
  if (contents_.size() - e->data_off < e->data_len) {
    fail(log_, "bogus size in bootfs header!");
  }

  // Clone a private, read-only snapshot of the file's subset of the bootfs VMO.
  zx::vmo file_vmo;
  zx_status_t status = vmo_.create_child(ZX_VMO_CHILD_SNAPSHOT | ZX_VMO_CHILD_NO_WRITE, e->data_off,
                                         e->data_len, &file_vmo);
  check(log_, status, "zx_vmo_create_child failed");

  status = file_vmo.set_property(ZX_PROP_NAME, filename.data(), filename.size());
  check(log_, status, "failed to set ZX_PROP_NAME");

  uint64_t size = e->data_len;
  status = file_vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size));
  check(log_, status, "failed to set ZX_PROP_VMO_CONTENT_SIZE");

  status = file_vmo.replace_as_executable(vmex_resource_, &file_vmo);
  check(log_, status, "zx_vmo_replace_as_executable failed");

  return file_vmo;
}
