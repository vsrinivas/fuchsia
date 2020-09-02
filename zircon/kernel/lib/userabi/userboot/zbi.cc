// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "zbi.h"

#include <lib/zbitl/view.h>
#include <lib/zbitl/vmo.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

#include "decompressor.h"
#include "util.h"

namespace {

constexpr const char kBootfsVmoName[] = "uncompressed-bootfs";

zx::vmo DecompressBootfs(const zx::debuglog& log, const zx::vmar& vmar_self, const zx::vmo& zbi_vmo,
                         uint64_t payload_offset, uint32_t payload_size,
                         uint32_t uncompressed_size) {
  zx::vmo bootfs_vmo;
  zx_status_t status = zx::vmo::create(uncompressed_size, 0, &bootfs_vmo);
  check(log, status, "cannot create BOOTFS VMO (%u bytes)", uncompressed_size);
  status = zbi_decompress(log, vmar_self, zbi_vmo, payload_offset, payload_size, bootfs_vmo, 0,
                          uncompressed_size);
  check(log, status, "failed to decompress BOOTFS");
  printl(log, "decompressed BOOTFS to VMO!\n");
  return bootfs_vmo;
}

zx::vmo ExtractBootfs(const zx::debuglog& log, const zx::vmar& vmar_self, const zx::vmo& zbi_vmo,
                      uint64_t payload_offset, uint32_t payload_size) {
  zx::vmo bootfs_vmo;

  if (payload_offset % ZX_PAGE_SIZE == 0) {
    zx_status_t status =
        zbi_vmo.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, payload_offset, payload_size, &bootfs_vmo);
    check(log, status, "zx_vmo_create_child failed for BOOTFS");
    printl(log, "cloned uncompressed BOOTFS to VMO!\n");
    return bootfs_vmo;
  }

  // The data is not aligned, so it must be copied into a new VMO.
  zx_status_t status = zx::vmo::create(payload_size, 0, &bootfs_vmo);
  check(log, status, "cannot create BOOTFS VMO (%u bytes)", payload_size);
  uintptr_t bootfs_payload;
  status = vmar_self.map(0, bootfs_vmo, 0, payload_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                         &bootfs_payload);
  check(log, status, "cannot map BOOTFS VMO (%u bytes)", payload_size);
  status = zbi_vmo.read(reinterpret_cast<void*>(bootfs_payload), payload_offset, payload_size);
  check(log, status, "cannot read BOOTFS into VMO (%u bytes)", payload_size);
  status = vmar_self.unmap(bootfs_payload, payload_size);
  check(log, status, "cannot unmap BOOTFS VMO (%u bytes)", payload_size);

  printl(log, "copied uncompressed BOOTFS to VMO!\n");
  return bootfs_vmo;
}

}  // namespace

zx::vmo GetBootfsFromZbi(const zx::debuglog& log, const zx::vmar& vmar_self,
                         const zx::vmo& zbi_vmo) {
  zbitl::PermissiveView<zbitl::MapUnownedVmo> zbi(
      zbitl::MapUnownedVmo{zx::unowned_vmo{zbi_vmo}, zx::unowned_vmar{vmar_self}});

  for (auto it = zbi.begin(); it != zbi.end(); ++it) {
    auto [header, payload] = *it;
    if (header->type == ZBI_TYPE_STORAGE_BOOTFS) {
      zx::vmo bootfs_vmo =
          (header->flags & ZBI_FLAG_STORAGE_COMPRESSED)
              ? DecompressBootfs(log, vmar_self, zbi_vmo, payload, header->length, header->extra)
              : ExtractBootfs(log, vmar_self, zbi_vmo, payload, header->length);
      bootfs_vmo.set_property(ZX_PROP_NAME, kBootfsVmoName, sizeof(kBootfsVmoName) - 1);

      // Signal that we've already processed this one.
      // GCC's -Wmissing-field-initializers is buggy: it should allow
      // designated initializers without all fields, but doesn't (in C++?).
      zbi_header_t discard{};
      discard.type = ZBI_TYPE_DISCARD;
      if (auto ok = zbi.EditHeader(it, discard); ok.is_error()) {
        check(log, ok.error_value(), "zx_vmo_write failed on ZBI VMO\n");
      }

      // Cancel error-checking since we're ending the iteration on purpose.
      zbi.ignore_error();
      return bootfs_vmo;
    }
  }

  if (auto check = zbi.take_error(); check.is_error()) {
    auto error = check.error_value();
    if (error.storage_error) {
      fail(log, "invalid ZBI: %.*s at offset %#x\n", static_cast<int>(error.zbi_error.size()),
           error.zbi_error.data(), error.item_offset);
    } else {
      fail(log, "invalid ZBI: %.*s at offset %#x: %s\n", static_cast<int>(error.zbi_error.size()),
           error.zbi_error.data(), error.item_offset, zx_status_get_string(*error.storage_error));
    }
  } else {
    fail(log, "no '/boot' bootfs in bootstrap message\n");
  }
}
