// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootdata.h"

#include <lib/hermetic-decompressor/hermetic-decompressor.h>
#include <string.h>
#include <zircon/boot/bootdata.h>
#include <zircon/boot/image.h>
#include <zircon/syscalls.h>

#include "util.h"

namespace {

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

}  // namespace

zx_handle_t bootdata_get_bootfs(zx_handle_t log, zx_handle_t vmar_self, zx_handle_t job,
                                zx_handle_t engine_vmo, zx_handle_t vdso_vmo,
                                zx_handle_t bootdata_vmo) {
  size_t off = 0;
  for (;;) {
    bootdata_t bootdata;
    zx_status_t status = zx_vmo_read(bootdata_vmo, &bootdata, off, sizeof(bootdata));
    check(log, status, "zx_vmo_read failed on bootdata VMO");
    if (!(bootdata.flags & BOOTDATA_FLAG_V2)) {
      fail(log, "bootdata v1 no longer supported");
    }

    switch (bootdata.type) {
      case BOOTDATA_CONTAINER:
        if (off == 0) {
          // Quietly skip container header.
          bootdata.length = 0;
        } else {
          fail(log, "container in the middle of bootdata");
        }
        break;

      case BOOTDATA_BOOTFS_BOOT: {
        zx::vmo bootfs_vmo;
        if (bootdata.flags & ZBI_FLAG_STORAGE_COMPRESSED) {
          status = zx::vmo::create(bootdata.extra, 0, &bootfs_vmo);
          check(log, status, "cannot create BOOTFS VMO (%u bytes)", bootdata.extra);
          status = Decompressor(job, engine_vmo, vdso_vmo)(*zx::unowned_vmo{bootdata_vmo},
                                                           off + sizeof(bootdata), bootdata.length,
                                                           bootfs_vmo, 0, bootdata.extra);
          check(log, status, "failed to decompress BOOTFS");
        } else {
          fail(log, "uncompressed BOOTFS not supported");
        }

        // Signal that we've already processed this one.
        bootdata.type = BOOTDATA_BOOTFS_DISCARD;
        check(log,
              zx_vmo_write(bootdata_vmo, &bootdata.type, off + offsetof(bootdata_t, type),
                           sizeof(bootdata.type)),
              "zx_vmo_write failed on bootdata VMO\n");

        printl(log, "decompressed bootfs to VMO!\n");
        return bootfs_vmo.release();
      }
    }

    off += BOOTDATA_ALIGN(sizeof(bootdata) + bootdata.length);
  }

  fail(log, "no '/boot' bootfs in bootstrap message\n");
}
