// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Device independent functions to validate partition data and disk images.
// Tools to validate

#include "validation.h"

#include <lib/cksum.h>
#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/span.h>

#include "device-partitioner.h"
#include "pave-logging.h"

namespace paver {

namespace {

// Determine if the CRC of the given zbi_header_t is valid.
//
// We require that the "hdr" has "hdr->length" valid bytes after it.
bool ZbiHeaderCrcValid(const zbi_header_t* hdr) {
  // If we don't have the CRC32 flag set, ensure no crc32 value is given.
  if ((hdr->flags & ZBI_FLAG_CRC32) == 0) {
    return (hdr->crc32 == ZBI_ITEM_NO_CRC32);
  }

  // Otherwise, calculate the CRC.
  return hdr->crc32 == crc32(0, reinterpret_cast<const uint8_t*>(hdr + 1), hdr->length);
}

}  // namespace

// Checks first few bytes of buffer to ensure it is a valid ZBI containing a kernel image.
//
// Also validates architecture in kernel header matches the target.
bool IsValidKernelZbi(Arch arch, fbl::Span<const uint8_t> data) {
  // Validate data header.
  if (data.size() < sizeof(zircon_kernel_t)) {
    ERROR("Data too short: expected at least %ld byte(s), got %ld byte(s).\n",
          sizeof(zircon_kernel_t), data.size());
    return false;
  }

  // Validate the container header.
  const auto payload = reinterpret_cast<const zircon_kernel_t*>(data.data());
  if (payload->hdr_file.type != ZBI_TYPE_CONTAINER ||
      payload->hdr_file.extra != ZBI_CONTAINER_MAGIC || payload->hdr_file.magic != ZBI_ITEM_MAGIC ||
      payload->hdr_file.flags != ZBI_FLAG_VERSION || payload->hdr_file.crc32 != ZBI_ITEM_NO_CRC32) {
    ERROR("Payload header has incorrect magic values, types, or flag.\n");
    return false;
  }
  if (payload->hdr_file.length > data.size() - offsetof(zircon_kernel_t, hdr_kernel)) {
    ERROR("Payload header length of %u byte(s) exceeds data available of %ld byte(s).\n",
          payload->hdr_file.length, data.size() - offsetof(zircon_kernel_t, hdr_kernel));
    return false;
  }

  // Validate the kernel header
  const uint32_t expected_kernel =
      (arch == Arch::kX64) ? ZBI_TYPE_KERNEL_X64 : ZBI_TYPE_KERNEL_ARM64;
  if (payload->hdr_kernel.type != expected_kernel || payload->hdr_kernel.magic != ZBI_ITEM_MAGIC ||
      (payload->hdr_kernel.flags & ZBI_FLAG_VERSION) != ZBI_FLAG_VERSION) {
    ERROR("Kernel header has invalid magic, architecture, or version.\n");
    return false;
  }
  if (payload->hdr_kernel.length > data.size() - offsetof(zircon_kernel_t, data_kernel)) {
    ERROR("Kernel header length of %u byte(s) exceeds data available of %ld byte(s).\n",
          payload->hdr_kernel.length, data.size() - offsetof(zircon_kernel_t, data_kernel));
    return false;
  }

  // Validate checksum if available.
  if (!ZbiHeaderCrcValid(&payload->hdr_kernel)) {
    ERROR("Kernel payload CRC invalid.\n");
    return false;
  }

  return true;
}

}  // namespace paver
