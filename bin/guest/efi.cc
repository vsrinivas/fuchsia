// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "efi.h"

static const uint16_t kMzSignature = 0x5a4d;  // MZ
static const uint32_t kMzMagic = 0x644d5241;  // ARM\x64

// MZ header used to boot ARM64 kernels.
//
// See: https://www.kernel.org/doc/Documentation/arm64/booting.txt.
struct MzHeader {
  uint32_t code0;
  uint32_t code1;
  uint64_t kernel_off;
  uint64_t kernel_len;
  uint64_t flags;
  uint64_t reserved0;
  uint64_t reserved1;
  uint64_t reserved2;
  uint32_t magic;
  uint32_t pe_off;
} __PACKED;
static_assert(sizeof(MzHeader) == 64, "");

static bool is_mz(const MzHeader* header) {
  return (header->code0 & UINT16_MAX) == kMzSignature &&
         header->kernel_len > sizeof(MzHeader) && header->magic == kMzMagic &&
         header->pe_off >= sizeof(MzHeader);
}

zx_status_t read_efi(const uintptr_t first_page,
                     uintptr_t* guest_ip,
                     uintptr_t* kernel_off,
                     uintptr_t* kernel_len) {
  MzHeader* mz_header = reinterpret_cast<MzHeader*>(first_page);
  if (!is_mz(mz_header))
    return ZX_ERR_NOT_SUPPORTED;

  *guest_ip = mz_header->kernel_off;
  *kernel_off = mz_header->kernel_off;
  *kernel_len = mz_header->kernel_len;
  return ZX_OK;
}
