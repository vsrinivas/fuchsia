// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_CORE_H_
#define SRC_LIB_ZXDUMP_CORE_H_

#include <lib/elfldltl/layout.h>

namespace zxdump {

// Zircon core dumps are always in the 64-bit little-endian ELF format.
using Elf = elfldltl::Elf64<elfldltl::ElfData::k2Lsb>;

}  // namespace zxdump

#endif  // SRC_LIB_ZXDUMP_CORE_H_
