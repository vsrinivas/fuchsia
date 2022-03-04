// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013 Google, Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <lib/console.h>
#include <lib/symbolizer-markup/writer.h>
#include <lib/version.h>
#include <lib/version/version-string.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ktl/byte.h>
#include <ktl/span.h>
#include <lk/init.h>
#include <vm/vm.h>

// This is allocated with sufficient size to be filled in later.  The contents
// have to be nonzero so they get allocated but don't otherwise matter.  See
// kernel-image.inc for more details.
extern "C" const char kVersionString[VERSION_STRING_SIZE] = "...";

namespace {

// If the build ID were SHA256, it would be 32 bytes.
// (The algorithms used for build IDs today actually produce fewer than that.)
// This string needs 2 bytes to print each byte in hex, plus a NUL terminator.
char gElfBuildIdString[65];

// Standard ELF note layout (Elf{32,64}_Nhdr in <elf.h>).
// The name and type fields' values are what GNU and GNU-compatible
// tools (i.e. everything in the Unix-like world in recent years)
// specify for build ID notes.
struct build_id_note {
  uint32_t namesz;
  uint32_t descsz;
  uint32_t type;
#define NT_GNU_BUILD_ID 3
#define NOTE_NAME "GNU"
  char name[(sizeof(NOTE_NAME) + 3) & -4];
  ktl::byte id[];
};

extern "C" const struct build_id_note __build_id_note_start;
extern "C" const ktl::byte __build_id_note_end[];

void init_build_id(uint level) {
  const build_id_note* const note = &__build_id_note_start;
  if (note->type != NT_GNU_BUILD_ID || note->namesz != sizeof(NOTE_NAME) ||
      memcmp(note->name, NOTE_NAME, sizeof(NOTE_NAME)) != 0 ||
      &note->id[note->descsz] != __build_id_note_end) {
    panic("ELF build ID note has bad format!\n");
  }
  ktl::span<const ktl::byte> id = ElfBuildId();
  if (id.size() * 2 >= sizeof(gElfBuildIdString)) {
    panic("ELF build ID is %zu bytes, expected %zu or fewer\n", id.size(),
          sizeof(gElfBuildIdString) / 2);
  }
  for (size_t i = 0; i < id.size(); ++i) {
    snprintf(&gElfBuildIdString[i * 2], 3, "%02x", static_cast<unsigned int>(id[i]));
  }
}

// This must happen before print_version below and should happen as early as possible to ensure we
// get useful backtraces when the kernel panics.
LK_INIT_HOOK(elf_build_id, &init_build_id, LK_INIT_LEVEL_EARLIEST)

}  // namespace

const char* version_string() { return kVersionString; }

const char* elf_build_id_string() { return gElfBuildIdString; }

ktl::span<const ktl::byte> ElfBuildId() {
  const build_id_note* const note = &__build_id_note_start;
  return {note->id, note->descsz};
}

void print_version() {
  dprintf(ALWAYS, "version:\n");
  dprintf(ALWAYS, "\tarch:     %s\n", ARCH);
  dprintf(ALWAYS, "\tzx_system_get_version_string: %s\n", kVersionString);
  dprintf(ALWAYS, "\tELF build ID: %s\n", gElfBuildIdString);
  dprintf(ALWAYS, "\tLK_DEBUGLEVEL: %d\n", LK_DEBUGLEVEL);
}

void PrintSymbolizerContext(FILE* f) {
  auto code_start = reinterpret_cast<uintptr_t>(__code_start);
  auto code_end = reinterpret_cast<uintptr_t>(__code_start);
  auto rodata_start = reinterpret_cast<uintptr_t>(__rodata_start);
  auto rodata_end = reinterpret_cast<uintptr_t>(__rodata_end);
  auto data_start = reinterpret_cast<uintptr_t>(__data_start);
  auto data_end = reinterpret_cast<uintptr_t>(__data_end);
  auto bss_start = reinterpret_cast<uintptr_t>(__bss_start);
  auto bss_end = reinterpret_cast<uintptr_t>(_end);
  auto bias = uintptr_t{KERNEL_BASE} - code_start;

  // A canonical module ID for the kernel.
  constexpr unsigned int kId = 0;

  // The four mappings match the mappings printed by vm_init().
  symbolizer_markup::Writer writer([f](std::string_view s) { f->Write(s); });
  writer.Reset()
      .ElfModule(kId, "kernel"sv, ElfBuildId())
      .LoadImageMmap(code_start, code_end - code_start, kId, {.read = true, .execute = true},
                     code_start + bias)
      .LoadImageMmap(rodata_start, rodata_end - rodata_start, kId, {.read = true},
                     rodata_start + bias)
      .LoadImageMmap(data_start, data_end - data_start, kId, {.read = true, .write = true},
                     data_start + bias)
      .LoadImageMmap(bss_start, bss_end - bss_start, kId, {.read = true, .write = true},
                     bss_start + bias);
}

void print_backtrace_version_info(FILE* f) {
  fprintf(f, "zx_system_get_version_string %s\n\n", kVersionString);

  // Log the ELF build ID in the format the symbolizer scripts understand.
  if (gElfBuildIdString[0] != '\0') {
    PrintSymbolizerContext(f);
    fprintf(f, "dso: id=%s base=%#lx name=zircon.elf\n", gElfBuildIdString,
            reinterpret_cast<uintptr_t>(__code_start));
  }
}

static int cmd_version(int argc, const cmd_args* argv, uint32_t flags) {
  print_version();
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("version", "print version", &cmd_version)
STATIC_COMMAND_END(version)

static void print_version_init(uint level) { print_version(); }

// print the version string if any level of debug is set
LK_INIT_HOOK(version, print_version_init, LK_INIT_LEVEL_HEAP - 1)
