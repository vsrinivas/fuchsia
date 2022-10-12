// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Device independent functions to validate partition data and disk images.
// Tools to validate

#include "validation.h"

#include <lib/cksum.h>
#include <lib/stdcompat/span.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "device-partitioner.h"
#include "pave-logging.h"

namespace paver {

namespace {

// Magic header of ChromeOS kernel verification block.
constexpr std::string_view kChromeOsMagicHeader = "CHROMEOS";

// Determine if the CRC of the given zbi_header_t is valid.
//
// We require that the "hdr" has "hdr->length" valid bytes after it.
bool ZbiHeaderCrcValid(const zbi_header_t* hdr) {
  // If we don't have the CRC32 flag set, ensure no crc32 value is given.
  if ((hdr->flags & ZBI_FLAGS_CRC32) == 0) {
    return (hdr->crc32 == ZBI_ITEM_NO_CRC32);
  }

  // Otherwise, calculate the CRC.
  return hdr->crc32 == crc32(0, reinterpret_cast<const uint8_t*>(hdr + 1), hdr->length);
}

}  // namespace

bool ExtractZbiPayload(cpp20::span<const uint8_t> data, const zbi_header_t** header,
                       cpp20::span<const uint8_t>* payload) {
  // Validate data header.
  if (data.size() < sizeof(zbi_header_t)) {
    ERROR("Data too short: expected at least %ld byte(s), got %ld byte(s).\n", sizeof(zbi_header_t),
          data.size());
    return false;
  }

  // Validate the header.
  const auto zbi_header = reinterpret_cast<const zbi_header_t*>(data.data());
  if (zbi_header->magic != ZBI_ITEM_MAGIC) {
    ERROR("ZBI header has incorrect magic value.\n");
    return false;
  }
  if ((zbi_header->flags & ZBI_FLAGS_VERSION) != ZBI_FLAGS_VERSION) {
    ERROR("ZBI header has invalid version.\n");
    return false;
  }

  // Ensure the data length is valid. We are okay with additional bytes
  // at the end of the data, but not having too few bytes available.
  if (zbi_header->length > data.size() - sizeof(zbi_header_t)) {
    ERROR("Header length length of %u byte(s) exceeds data available of %ld byte(s).\n",
          zbi_header->length, data.size() - sizeof(zbi_header_t));
    return false;
  }

  // Verify CRC.
  if (!ZbiHeaderCrcValid(zbi_header)) {
    ERROR("ZBI payload CRC invalid.\n");
    return false;
  }

  // All good.
  *header = zbi_header;
  *payload = data.subspan(sizeof(zbi_header_t), zbi_header->length);
  return true;
}

bool IsValidKernelZbi(Arch arch, cpp20::span<const uint8_t> data) {
  // Get container header.
  const zbi_header_t* container_header;
  cpp20::span<const uint8_t> container_data;
  if (!ExtractZbiPayload(data, &container_header, &container_data)) {
    return false;
  }

  // Ensure it is of the correct type.
  if (container_header->type != ZBI_TYPE_CONTAINER) {
    ERROR("ZBI container not a container type, or has invalid magic value.\n");
    return false;
  }
  if (container_header->extra != ZBI_CONTAINER_MAGIC) {
    ERROR("ZBI container has invalid magic value.\n");
    return false;
  }

  // Extract kernel.
  const zbi_header_t* kernel_header;
  cpp20::span<const uint8_t> kernel_data;
  if (!ExtractZbiPayload(container_data, &kernel_header, &kernel_data)) {
    return false;
  }

  // Ensure it is of the correct type.
  const uint32_t expected_kernel_type =
      (arch == Arch::kX64) ? ZBI_TYPE_KERNEL_X64 : ZBI_TYPE_KERNEL_ARM64;
  if (kernel_header->type != expected_kernel_type) {
    ERROR("ZBI kernel payload has incorrect type or architecture. Expected %#08x, got %#08x.\n",
          expected_kernel_type, kernel_header->type);
    return false;
  }

  // Ensure payload contains enough data for the kernel header.
  if (kernel_header->length < sizeof(zbi_kernel_t)) {
    ERROR("ZBI kernel payload too small.\n");
    return false;
  }

  return true;
}

bool IsValidChromeOSKernel(cpp20::span<const uint8_t> data) {
  // Ensure the data contains the ChromeOS verification block magic
  // signature.
  //
  // See https://www.chromium.org/chromium-os/chromiumos-design-docs/disk-format
  if (data.size() < kChromeOsMagicHeader.size()) {
    ERROR("ChromeOS kernel payload too small.\n");
    return false;
  }
  if (memcmp(data.data(), kChromeOsMagicHeader.data(), kChromeOsMagicHeader.size()) != 0) {
    ERROR("ChromeOS kernel magic header invalid.\n");
    return false;
  }

  return true;
}

}  // namespace paver
