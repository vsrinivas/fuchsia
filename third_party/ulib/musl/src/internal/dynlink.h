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

typedef void (*stage2_func)(unsigned char*, void*);
// stage3_funcs should be __attribute__((noreturn)), but this cannot
// be expressed in the typedef.
typedef void (*stage3_func)(void*);
