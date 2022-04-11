// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_CORE_H_
#define SRC_LIB_ZXDUMP_CORE_H_

#include <lib/elfldltl/layout.h>

#include <string_view>

namespace zxdump {

// Zircon core dumps are always in the 64-bit little-endian ELF format.
using Elf = elfldltl::Elf64<elfldltl::ElfData::k2Lsb>;

// Note headers, names, and descriptions are aligned in the file.
// The alignment padding is not included in n_namesz or n_descsz.
// Note n_namesz does include the mandatory NUL terminator.
inline constexpr uint32_t NoteAlign(size_t note_size = 1) {
  return Elf::Nhdr::Align(static_cast<uint32_t>(note_size));
}

// These note names in an ET_CORE file give data about a process.
//
// The n_type field contains the ZX_INFO_* or ZX_PROP_* value and the contents
// of the note are exactly as returned by get_info / get_property.
inline constexpr std::string_view kProcessInfoNoteName{"ZirconProcessInfo"};
inline constexpr std::string_view kProcessPropertyNoteName{"ZirconProcessProperty"};

}  // namespace zxdump

#endif  // SRC_LIB_ZXDUMP_CORE_H_
