// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_C_SANITIZERS_FUCHSIA_IO_CONSTANTS_H_
#define ZIRCON_SYSTEM_ULIB_C_SANITIZERS_FUCHSIA_IO_CONSTANTS_H_

#include <stdint.h>
#include <zircon/fidl.h>

// This definition of a bit of fuchsia.io.Directory/Open is here because llcpp has dependencies on
// new/delete, which aren't possible when this code is used in libc.
constexpr uint64_t fuchsia_io_MAX_PATH = 4096;
constexpr uint32_t fuchsia_io_OPEN_RIGHT_READABLE = 1;
constexpr uint32_t fuchsia_io_OPEN_RIGHT_WRITABLE = 2;
constexpr uint64_t fuchsia_io_DirectoryOpenOrdinal = 0x2C5044561D685EC0ull;
struct fuchsia_io_DirectoryOpenRequest {
  FIDL_ALIGNDECL
  fidl_message_header_t hdr;
  uint32_t flags;
  uint32_t mode;
  fidl_string_t path;
  zx_handle_t object;
};

#endif  // ZIRCON_SYSTEM_ULIB_C_SANITIZERS_FUCHSIA_IO_CONSTANTS_H_
