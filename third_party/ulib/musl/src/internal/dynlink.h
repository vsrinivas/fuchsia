#pragma once

#include <elf.h>
#include <features.h>
#include <stddef.h>
#include <stdint.h>

#if UINTPTR_MAX == 0xffffffff
typedef Elf32_Ehdr Ehdr;
typedef Elf32_Phdr Phdr;
typedef Elf32_Sym Sym;
#define R_TYPE(x) ((x)&255)
#define R_SYM(x) ((x) >> 8)
#define R_INFO ELF32_R_INFO
#else
typedef Elf64_Ehdr Ehdr;
typedef Elf64_Phdr Phdr;
typedef Elf64_Sym Sym;
#define R_TYPE(x) ((x)&0x7fffffff)
#define R_SYM(x) ((x) >> 32)
#define R_INFO ELF64_R_INFO
#endif

/* These enum constants provide unmatchable default values for
 * any relocation type the arch does not use. */
enum {
    REL_NONE = 0,
    REL_SYMBOLIC = -100,
    REL_GOT,
    REL_PLT,
    REL_RELATIVE,
    REL_OFFSET,
    REL_OFFSET32,
    REL_COPY,
    REL_SYM_OR_REL,
    REL_DTPMOD,
    REL_DTPOFF,
    REL_TPOFF,
    REL_TPOFF_NEG,
    REL_TLSDESC,
    REL_FUNCDESC,
    REL_FUNCDESC_VAL,
};

#include "reloc.h"

#define IS_RELATIVE(x, s) \
    ((R_TYPE(x) == REL_RELATIVE) || (R_TYPE(x) == REL_SYM_OR_REL && !R_SYM(x)))

#ifndef NEED_MIPS_GOT_RELOCS
#define NEED_MIPS_GOT_RELOCS 0
#endif

#ifndef DT_DEBUG_INDIRECT
#define DT_DEBUG_INDIRECT 0
#endif

#define AUX_CNT 32
#define DYN_CNT 32

// This is the return value of the dynamic linker startup functions.
// They return all the way back to _start so as to pop their stack
// frames.  The DL_START_ASM code at _start then receives these two
// values and jumps to the entry point with the argument in place for
// the C ABI and return address/frame pointer cleared so it's the base
// of the call stack.
#ifndef DL_START_RETURN
typedef struct {
    void* arg;
    void* entry;
} dl_start_return_t;
#define DL_START_RETURN(entry, arg) \
    (dl_start_return_t) { (arg), (entry) }
#endif

typedef dl_start_return_t stage2_func(void* start_arg, void* vdso);
typedef dl_start_return_t stage3_func(void* start_arg);

// We can access these with simple PC-relative relocs.
// _BASE is defined by base.ld to 0, i.e. the lowest address in the DSO image.
// _DYNAMIC is defined automagically by the linker.
extern const char _BASE[] __attribute__((visibility("hidden")));
extern size_t _DYNAMIC[] __attribute__((visibility("hidden")));
