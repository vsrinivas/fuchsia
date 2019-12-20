// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013 Google, Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <lib/version.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <lk/init.h>
#include <vm/vm.h>

/* generated for us */
#include <config-buildid.h>

/* BUILDID is optional, and may be defined anywhere */
#ifndef BUILDID
#define BUILDID ""
#endif

// If the build ID were SHA256, it would be 32 bytes.
// (The algorithms used for build IDs today actually produce fewer than that.)
// This string needs 2 bytes to print each byte in hex, plus a NUL terminator.
static char elf_build_id_string[65];

const lk_version_t version = {
    .struct_version = VERSION_STRUCT_VERSION,
    .arch = ARCH,
    .buildid = BUILDID,
    .elf_build_id = elf_build_id_string,
};

void print_version(void) {
  printf("version:\n");
  printf("\tarch:     %s\n", version.arch);
  printf("\tbuildid:  %s\n", version.buildid);
  printf("\tELF build ID: %s\n", version.elf_build_id);
}

// TODO(eieio): Consider whether it makes sense to locate the logic for printing
// mappings somewhere else (perhaps in vm/vm.cpp?).
static void print_mmap(uintptr_t bias, const void* begin, const void* end, const char* perm) {
  const uintptr_t start = reinterpret_cast<uintptr_t>(begin);
  const size_t size = reinterpret_cast<uintptr_t>(end) - start;
  printf("{{{mmap:%#lx:%#lx:load:0:%s:%#lx}}}\n", start, size, perm, start + bias);
}

void print_backtrace_version_info() {
  printf("BUILDID %s\n\n", version.buildid);

  // Log the ELF build ID in the format the symbolizer scripts understand.
  if (version.elf_build_id[0] != '\0') {
    const uintptr_t bias = KERNEL_BASE - reinterpret_cast<uintptr_t>(__code_start);
    print_module(_printf, version.elf_build_id);
    // These four mappings match the mappings printed by vm_init().
    print_mmap(_printf, bias, __code_start, __code_end, "rx");
    print_mmap(_printf, bias, __rodata_start, __rodata_end, "r");
    print_mmap(_printf, bias, __data_start, __data_end, "rw");
    print_mmap(_printf, bias, __bss_start, _end, "rw");
    printf("dso: id=%s base=%#lx name=zircon.elf\n", version.elf_build_id,
           reinterpret_cast<uintptr_t>(__code_start));
  }
}

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
  uint8_t id[];
};

extern const struct build_id_note __build_id_note_start;
extern const uint8_t __build_id_note_end[];

static void init_build_id(uint level) {
  const struct build_id_note* const note = &__build_id_note_start;
  if (note->type != NT_GNU_BUILD_ID || note->namesz != sizeof(NOTE_NAME) ||
      memcmp(note->name, NOTE_NAME, sizeof(NOTE_NAME)) != 0 ||
      &note->id[note->descsz] != __build_id_note_end) {
    panic("ELF build ID note has bad format!\n");
  }
  if (note->descsz * 2 >= sizeof(elf_build_id_string)) {
    panic("ELF build ID is %u bytes, expected %u or fewer\n", note->descsz,
          (uint32_t)(sizeof(elf_build_id_string) / 2));
  }
  for (uint32_t i = 0; i < note->descsz; ++i) {
    snprintf(&elf_build_id_string[i * 2], 3, "%02x", note->id[i]);
  }
}

// This must happen before print_version, below.
LK_INIT_HOOK(elf_build_id, &init_build_id, LK_INIT_LEVEL_HEAP - 2)

#include <debug.h>
#include <lib/console.h>

static int cmd_version(int argc, const cmd_args* argv, uint32_t flags) {
  print_version();
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("version", "print version", &cmd_version)
STATIC_COMMAND_END(version)

#if LK_DEBUGLEVEL > 0
static void print_version_init(uint) { print_version(); }

// print the version string if any level of debug is set
LK_INIT_HOOK(version, print_version_init, LK_INIT_LEVEL_HEAP - 1)
#endif
