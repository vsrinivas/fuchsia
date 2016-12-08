// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dje): This code is copied from crashlogger. Modest changes
// have been done. Further changes left for later.
// TODO(dje): Use ELF library eventually.
// TODO(dje): Runtime 32/64 support. Later, if ever needed.

#pragma once

#include <cstddef>
#include <climits>
#include <elf.h>

#include <magenta/types.h>

#include "memory.h"

namespace debugserver {
namespace elf {

#if UINT_MAX == ULONG_MAX
using ehdr_type = Elf32_Ehdr;
using phdr_type = Elf32_Phdr;
#else
using ehdr_type = Elf64_Ehdr;
using phdr_type = Elf64_Phdr;
#endif

// Maximum length in bytes of a build id.
constexpr size_t kMaxBuildIdSize = 64;

bool ReadElfHdr(const util::Memory& m, mx_vaddr_t base, ehdr_type* hdr);

bool VerifyElfHdr(const ehdr_type* hdr);

// Store the build id, if present, in |buf|.
// |buf_size| must be at least kMaxBuildIdSize * 2 + 1.
// |hdr| is the ELF header, generally fetched with ReadElfHdr().
// It must have already been verified with VerifyElfHdr().
// Returns a boolean indicating success.
// If a build id is not found buf is "" and true is returned.
// TODO(dje): As with other changes deferred for later,
// one might consider using std::string here.
bool ReadBuildId(const util::Memory& m,
                 mx_vaddr_t base,
                 const ehdr_type* hdr,
                 char* buf,
                 size_t buf_size);

}  // namespace elf
}  // namespace debugserver
