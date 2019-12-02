// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootdata.h"

#include <lib/hermetic-decompressor/hermetic-decompressor.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/boot/image.h>
#include <zircon/syscalls.h>

#include "util.h"

namespace {
constexpr const char kBootfsVmoName[] = "uncompressed-bootfs";

#ifdef ZBI_COMPRESSION_MAGIC

class EngineService {
 public:
  using Magic = HermeticDecompressorEngineService::Magic;
  static constexpr Magic kMagic = HermeticDecompressorEngineService::ZBI_COMPRESSION_MAGIC;

  EngineService(zx_handle_t job, zx_handle_t engine, zx_handle_t vdso)
      : job_(job), engine_(engine), vdso_(vdso) {}

  auto job() const { return zx::unowned_job{*job_}; }

  zx_status_t GetEngine(Magic magic, zx::unowned_vmo* vmo) {
    if (magic == kMagic) {
      *vmo = zx::unowned_vmo{engine_};
      return ZX_OK;
    }
    return ZX_ERR_NOT_FOUND;
  }

  zx_status_t GetVdso(zx::unowned_vmo* vmo) {
    *vmo = zx::unowned_vmo{vdso_->get()};
    return ZX_OK;
  }

 private:
  zx::unowned_job job_;
  zx::unowned_vmo engine_;
  zx::unowned_vmo vdso_;
};

using Decompressor = HermeticDecompressorWithEngineService<EngineService>;

#endif  // ZBI_COMPRESSION_MAGIC

}  // namespace

zx_handle_t bootdata_get_bootfs(zx_handle_t log, zx_handle_t vmar_self, zx_handle_t job,
                                zx_handle_t engine_vmo, zx_handle_t vdso_vmo,
                                zx_handle_t bootdata_vmo) {
  size_t off = 0;
  for (;;) {
    zbi_header_t bootdata;
    zx_status_t status = zx_vmo_read(bootdata_vmo, &bootdata, off, sizeof(bootdata));
    check(log, status, "zx_vmo_read failed on bootdata VMO");
    if (!(bootdata.flags & ZBI_FLAG_VERSION)) {
      fail(log, "bootdata v1 no longer supported");
    }

    switch (bootdata.type) {
      case ZBI_TYPE_CONTAINER:
        if (off == 0) {
          // Quietly skip container header.
          bootdata.length = 0;
        } else {
          fail(log, "container in the middle of bootdata");
        }
        break;

      case ZBI_TYPE_STORAGE_BOOTFS: {
        zx::vmo bootfs_vmo;
        if (bootdata.flags & ZBI_FLAG_STORAGE_COMPRESSED) {
#ifdef ZBI_COMPRESSION_MAGIC
          status = zx::vmo::create(bootdata.extra, 0, &bootfs_vmo);
          check(log, status, "cannot create BOOTFS VMO (%u bytes)", bootdata.extra);
          status = Decompressor(job, engine_vmo, vdso_vmo)(*zx::unowned_vmo{bootdata_vmo},
                                                           off + sizeof(bootdata), bootdata.length,
                                                           bootfs_vmo, 0, bootdata.extra);
          check(log, status, "failed to decompress BOOTFS");
#else
          fail(log, "userboot built without BOOTFS decompressor!");
#endif
        } else {
          off += sizeof(bootdata);
          if (off % ZX_PAGE_SIZE == 0) {
            status = zx_vmo_create_child(bootdata_vmo, ZX_VMO_CHILD_COPY_ON_WRITE, off,
                                         bootdata.length, bootfs_vmo.reset_and_get_address());
            check(log, status, "zx_vmo_create_child failed for BOOTFS");
          } else {
            // The data is not aligned, so it must be copied into a new VMO.
            status = zx::vmo::create(bootdata.length, 0, &bootfs_vmo);
            check(log, status, "cannot create BOOTFS VMO (%u bytes)", bootdata.length);
            zx::unowned_vmar vmar{vmar_self};
            uintptr_t bootfs_payload;
            status = vmar->map(0, bootfs_vmo, 0, bootdata.length,
                               ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &bootfs_payload);
            check(log, status, "cannot map BOOTFS VMO (%u bytes)", bootdata.length);
            status = zx_vmo_read(bootdata_vmo, reinterpret_cast<void*>(bootfs_payload), off,
                                 bootdata.length);
            check(log, status, "cannot read BOOTFS into VMO (%u bytes)", bootdata.length);
            status = vmar->unmap(bootfs_payload, bootdata.length);
            check(log, status, "cannot unmap BOOTFS VMO (%u bytes)", bootdata.length);
          }
        }
        bootfs_vmo.set_property(ZX_PROP_NAME, kBootfsVmoName, sizeof(kBootfsVmoName) - 1);

        // Signal that we've already processed this one.
        bootdata.type = ZBI_TYPE_DISCARD;
        check(log,
              zx_vmo_write(bootdata_vmo, &bootdata.type, off + offsetof(zbi_header_t, type),
                           sizeof(bootdata.type)),
              "zx_vmo_write failed on bootdata VMO\n");

        printl(log,
#ifdef ZBI_COMPRESSION_MAGIC
               "decompressed"
#else
               "copied uncompressed"
#endif
               " BOOTFS to VMO!\n");
        return bootfs_vmo.release();
      }
    }

    off += ZBI_ALIGN(static_cast<uint32_t>(sizeof(bootdata)) + bootdata.length);
  }

  fail(log, "no '/boot' bootfs in bootstrap message\n");
}
