// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013 Google, Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>

__BEGIN_CDECLS

#define VERSION_STRUCT_VERSION 0x2

typedef struct {
    unsigned int struct_version;
    const char *arch;
    const char *platform;
    const char *target;
    const char *project;
    const char *buildid;

    // This is a printable string of hex digits giving the ELF build ID
    // bits.  The ELF build ID is a unique bit-string identifying the
    // kernel binary, produced automatically by the linker.  It's the
    // canonical way to find a binary and its debug information.
    const char *elf_build_id;
} lk_version_t;

extern const lk_version_t version;

void print_version(void);

__END_CDECLS
