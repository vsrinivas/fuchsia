// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013 Google, Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_VERSION_INCLUDE_LIB_VERSION_H_
#define ZIRCON_KERNEL_LIB_VERSION_INCLUDE_LIB_VERSION_H_

#include <zircon/compiler.h>

#define VERSION_STRUCT_VERSION 0x3

// Global structure that holds some build information about the kernel.
typedef struct {
  unsigned int struct_version;
  const char* arch;
  const char* buildid;

  // This is a printable string of hex digits giving the ELF build ID
  // bits.  The ELF build ID is a unique bit-string identifying the
  // kernel binary, produced automatically by the linker.  It's the
  // canonical way to find a binary and its debug information.
  const char* elf_build_id;
} lk_version_t;

extern "C" const lk_version_t version;

void print_version(void);

// Prints version info and kernel mappings required to interpret backtraces.
void print_backtrace_version_info(void);

#endif  // ZIRCON_KERNEL_LIB_VERSION_INCLUDE_LIB_VERSION_H_
