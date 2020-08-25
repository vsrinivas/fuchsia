// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mbr.h"

#include <endian.h>
#include <inttypes.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <ddk/debug.h>

namespace mbr {

zx_status_t Parse(const uint8_t* buffer, size_t bufsz, Mbr* out) {
  if (bufsz < kMbrSize) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  // Check the boot signature first to sanity-check that the buffer looks like
  // an MBR header.
  uint16_t boot_signature =
      le16toh(*reinterpret_cast<const uint16_t*>(buffer + offsetof(Mbr, boot_signature)));
  if (boot_signature != kMbrBootSignature) {
    zxlogf(ERROR, "mbr: invalid mbr boot signature, expected 0x%04x got 0x%04x",
           kMbrBootSignature, boot_signature);
    return ZX_ERR_NOT_SUPPORTED;
  }
  memcpy(out, buffer, kMbrSize);
  // Correct endian-sensitive fields
  out->boot_signature = le16toh(out->boot_signature);
  for (auto& partition : out->partitions) {
    partition.start_sector_lba = le32toh(partition.start_sector_lba);
    partition.num_sectors = le32toh(partition.num_sectors);
  }
  return ZX_OK;
}

}  // namespace mbr
