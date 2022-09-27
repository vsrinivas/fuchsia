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

// Minimum size of an ELF file.
inline constexpr size_t kMinimumElf = sizeof(Elf::Ehdr);

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

// Many threads appear in the same ET_CORE file for one process.  These note
// names give information about each thread, and appear after all the process
// notes: first all the notes for one thread; then all the notes for the next
// thread; and so on.  The first note for each thread is ZX_INFO_HANDLE_BASIC,
// so that can be used to partition a run of thread notes in a core file into
// the set of notes for each separate thread.
//
// These are get_info / get_property for threads, as for processes above.
inline constexpr std::string_view kThreadInfoNoteName{"ZirconThreadInfo"};
inline constexpr std::string_view kThreadPropertyNoteName{"ZirconThreadProperty"};

// The n_type field contains the zx_thread_state_topic_t value and the contents
// of the note are exactly as returned by read_state.
inline constexpr std::string_view kThreadStateNoteName{"ZirconThreadState"};

// The n_type field is always zero.  The contents is a time_t value, i.e.
// 64-bit count of seconds since 1970-1-1T0:00 UTC.  (A note holding 0 claims
// to be a dump made in 1970; to elide the dump date, the note should be
// omitted entirely.)
inline constexpr std::string_view kDateNoteName{"ZirconDumpDate"};

// The contents are JSON, schema based on zx::system methods.
inline constexpr std::string_view kSystemNoteName{"ZirconSystem.json"};

}  // namespace zxdump

#endif  // SRC_LIB_ZXDUMP_CORE_H_
