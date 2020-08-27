// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/bootfs/parser.h"

#include <inttypes.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/boot/bootfs.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace bootfs {

Parser::~Parser() {
  if (dir_) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(dir_) - sizeof(zbi_bootfs_header_t);
    zx::vmar::root_self()->unmap(addr, MappingSize());
  }
}

zx_status_t Parser::Init(zx::unowned_vmo vmo) {
  if (dir_ != nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t status = vmo->get_size(&vmo_size_);
  if (status != ZX_OK) {
    printf("Parser::Init: couldn't get bootfs VMO size - %s\n", zx_status_get_string(status));
    return status;
  }

  zbi_bootfs_header_t hdr;
  status = vmo->read(&hdr, 0, sizeof(hdr));
  if (status != ZX_OK) {
    printf("Parser::Init: couldn't read boot_data - %s\n", zx_status_get_string(status));
    return status;
  }
  if (hdr.magic != ZBI_BOOTFS_MAGIC) {
    printf("Parser::Init: incorrect bootdata header: %x\n", hdr.magic);
    return ZX_ERR_IO;
  }
  zx_vaddr_t addr = 0;
  status =
      zx::vmar::root_self()->map(0, *vmo, 0, sizeof(hdr) + hdr.dirsize, ZX_VM_PERM_READ, &addr);
  if (status != ZX_OK) {
    printf("Parser::Init: couldn't map directory: %d\n", status);
    return status;
  }
  dirsize_ = hdr.dirsize;
  dir_ = reinterpret_cast<char*>(addr) + sizeof(hdr);
  return ZX_OK;
}

zx_status_t Parser::Parse(Callback callback) {
  if (dir_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  size_t avail = dirsize_;
  auto* p = static_cast<char*>(dir_);
  while (avail >= sizeof(zbi_bootfs_dirent_t)) {
    auto e = reinterpret_cast<zbi_bootfs_dirent_t*>(p);
    size_t dirent_size = ZBI_BOOTFS_DIRENT_SIZE(e->name_len);
    if ((dirent_size > avail) || (e->name_len < 1) || (e->name_len > ZBI_BOOTFS_MAX_NAME_LEN)) {
      printf("bootfs: invalid directory entry name length\n");
      return ZX_ERR_IO;
    }
    if ((e->name[0] == '/') || (e->name[e->name_len - 1] != 0)) {
      printf("bootfs: invalid directory entry name\n");
      return ZX_ERR_INVALID_ARGS;
    }
    if ((e->data_off % ZBI_BOOTFS_PAGE_SIZE) != 0) {
      printf("bootfs: directory entry data offset not page-aligned\n");
      return ZX_ERR_IO;
    }
    if (e->data_off > vmo_size_ || e->data_len > (vmo_size_ - e->data_off)) {
      printf("bootfs: directory entry data out-of-bounds\n");
      return ZX_ERR_IO;
    }

    zx_status_t status = callback(e);
    if (status != ZX_OK) {
      return status;
    }
    p += dirent_size;
    avail -= dirent_size;
  }
  if (avail > 0) {
    printf("bootfs: partial directory entry header\n");
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

}  // namespace bootfs
