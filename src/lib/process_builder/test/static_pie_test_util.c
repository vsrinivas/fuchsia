// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This program is used to test the process_builder library's handling of
// statically linked PIE executables.
//
// It implements just enough ELF parsing to look up syscall symbols from the
// vDSO using the GNU hash table, get the zx_channel_read/write symbols, read
// the processargs bootstrap message to find another channel handle with type
// PA_USER0, and then reads a message from that channel and echos it back on the
// same channel. The test uses this echo to confirm that the process was loaded
// correctly.

#include <elf.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

typedef void (*zx_process_exit_t)(int64_t /*retcode*/);

typedef zx_status_t (*zx_channel_write_t)(zx_handle_t /*handle*/,
                                          uint32_t /*options*/,
                                          const void* /*bytes*/,
                                          uint32_t /*num_bytes*/,
                                          const zx_handle_t* /*handles*/,
                                          uint32_t /*num_handles*/);

typedef zx_status_t (*zx_channel_read_t)(
    zx_handle_t /*handle*/, uint32_t /*options*/, void* /*bytes*/,
    zx_handle_t* /*handles*/, uint32_t /*num_bytes*/, uint32_t /*num_handles*/,
    uint32_t* /*actual_bytes*/, uint32_t* /*actual_handles*/);

// Get a memory load address from a base load address and unadjusted virtual
// address.
static void* laddr(const void* base, size_t vaddr) {
  return (uint8_t*)base + vaddr;
}

static const Elf64_Dyn* search_dyn(const Elf64_Dyn* dyn_array,
                                   Elf64_Sxword tag) {
  for (; dyn_array->d_tag != tag; ++dyn_array) {
    if (dyn_array->d_tag == DT_NULL) {
      return NULL;
    }
  }
  return dyn_array;
}

typedef struct {
  uint32_t nbuckets;
  uint32_t symoffset;
  uint32_t bloom_size;
  uint32_t bloom_shift;
  uint64_t* bloom;    // uint64_t[bloom_size]
  uint32_t* buckets;  // uint32_t[nbuckets]
  uint32_t* chain;
} GnuHashTable;

static GnuHashTable read_gnu_hash_table(const uint32_t* addr) {
  uint32_t nbuckets = addr[0];
  uint32_t bloom_size = addr[2];
  uint64_t* bloom = (uint64_t*)&addr[4];
  uint32_t* buckets = (uint32_t*)&bloom[bloom_size];
  uint32_t* chain = &buckets[nbuckets];

  GnuHashTable ret = {
      .nbuckets = nbuckets,
      .symoffset = addr[1],
      .bloom_size = bloom_size,
      .bloom_shift = addr[3],
      .bloom = bloom,
      .buckets = buckets,
      .chain = chain,
  };
  return ret;
}

static int strcmp(const char* l, const char* r) {
  while (*l == *r && *l) {
    ++l, ++r;
  }
  return *(unsigned char*)l - *(unsigned char*)r;
}

static uint32_t gnu_hash(const char* name) {
  const uint8_t* s = (uint8_t*)name;
  uint32_t hash = 5381;
  for (; *s; s++) {
    hash += hash * 32 + *s;
  }
  return hash;
}

static const Elf64_Sym* lookup_sym(const char* name,
                                   const GnuHashTable* hashtab,
                                   const Elf64_Sym* symtab,
                                   const char* strtab) {
  // Not bothering with bloom filter, don't need best possible lookup speed.

  uint32_t lookup_hash = gnu_hash(name);
  uint32_t bucket = lookup_hash % hashtab->nbuckets;
  uint32_t chain_start = hashtab->buckets[bucket];
  if (chain_start < hashtab->symoffset) {
    return NULL;
  }

  for (size_t i = chain_start;; ++i) {
    uint32_t chain_hash = hashtab->chain[i - hashtab->symoffset];
    const char* symname = strtab + symtab[i].st_name;
    if ((chain_hash | 1) == (lookup_hash | 1) && strcmp(name, symname) == 0) {
      return &symtab[i];
    }
    if (chain_hash & 1) {
      // Reached end of chain, lookup failed.
      break;
    }
  }
  return NULL;
}

static const void* lookup_func(const char* name, const GnuHashTable* hashtab,
                               const Elf64_Sym* symtab, const char* strtab,
                               const void* base) {
  const Elf64_Sym* sym = lookup_sym(name, hashtab, symtab, strtab);
  if (!sym || ELF64_ST_TYPE(sym->st_info) != STT_FUNC || !sym->st_value) {
    return NULL;
  }
  return laddr(base, sym->st_value);
}

// Entry point. Arguments are a handle to the bootstrap channel and the base
// address that the vDSO was loaded at.
void _start(zx_handle_t bootstrap_chan, void* vdso_base) {
  Elf64_Ehdr* ehdr = (Elf64_Ehdr*)vdso_base;

  // Find PT_DYNAMIC header.
  const Elf64_Phdr* phdr = laddr(vdso_base, ehdr->e_phoff);
  const Elf64_Phdr* phdr_dynamic = NULL;
  for (Elf64_Half ph = 0; ph < ehdr->e_phnum; ++ph) {
    if (phdr[ph].p_type == PT_DYNAMIC) {
      phdr_dynamic = &phdr[ph];
    }
  }
  if (!phdr_dynamic) {
    return;
  }

  // Find the GNU hash table, symbol table, and string table.
  const Elf64_Dyn* dyn_array = laddr(vdso_base, phdr_dynamic->p_vaddr);
  const Elf64_Dyn* dyn_gnu_hash = search_dyn(dyn_array, DT_GNU_HASH);
  const Elf64_Dyn* dyn_symtab = search_dyn(dyn_array, DT_SYMTAB);
  const Elf64_Dyn* dyn_strtab = search_dyn(dyn_array, DT_STRTAB);
  if (!dyn_gnu_hash || !dyn_symtab || !dyn_strtab) {
    return;
  }

  const GnuHashTable hashtab =
      read_gnu_hash_table(laddr(vdso_base, dyn_gnu_hash->d_un.d_val));
  const Elf64_Sym* symtab = laddr(vdso_base, dyn_symtab->d_un.d_val);
  const char* strtab = laddr(vdso_base, dyn_strtab->d_un.d_val);

  // Lookup the channel_read and channel_write syscalls from the vDSO
  zx_channel_read_t zx_channel_read =
      lookup_func("_zx_channel_read", &hashtab, symtab, strtab, vdso_base);
  zx_channel_write_t zx_channel_write =
      lookup_func("_zx_channel_write", &hashtab, symtab, strtab, vdso_base);
  if (!zx_channel_read || !zx_channel_write) {
    return;
  }

  // Read the bootstrap message from the bootstrap channel and find the PA_USER0
  // channel handle.
  uint8_t read_msg[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t read_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_bytes, actual_handles;
  if (zx_channel_read(bootstrap_chan, 0, read_msg, read_handles,
                      ARRAY_SIZE(read_msg), ARRAY_SIZE(read_handles),
                      &actual_bytes, &actual_handles) != ZX_OK) {
    return;
  }
  zx_proc_args_t* bootstrap_header = (zx_proc_args_t*)read_msg;
  uint32_t* handle_info =
      (uint32_t*)((uint8_t*)read_msg + bootstrap_header->handle_info_off);
  zx_handle_t user_chan = ZX_HANDLE_INVALID;
  for (uint32_t i = 0; i < actual_handles; ++i) {
    if (handle_info[i] == PA_HND(PA_USER0, 0)) {
      user_chan = read_handles[i];
      break;
    }
  }
  if (user_chan == ZX_HANDLE_INVALID) {
    return;
  }

  // Read a message from the PA_USER0 channel and echo it back. Note that
  // ZX_ERR_SHOULD_WAIT isn't handled here; the test should make sure to write
  // to the channel before starting us.
  if (zx_channel_read(user_chan, 0, read_msg, read_handles,
                      ARRAY_SIZE(read_msg), ARRAY_SIZE(read_handles),
                      &actual_bytes, &actual_handles) != ZX_OK) {
    return;
  }
  zx_channel_write(user_chan, 0, read_msg, actual_bytes, read_handles,
                   actual_handles);

  // Exit cleanly.
  zx_process_exit_t zx_process_exit =
      lookup_func("_zx_process_exit", &hashtab, symtab, strtab, vdso_base);
  zx_process_exit(0);
}

// Compiler emits calls to these so need to provide implementations.

void __stack_chk_fail(void) { __builtin_trap(); }

// From //zircon/third_party/ulib/musl/src/string/memset.c
void* memset(void* dest, int c, size_t n) {
  unsigned char* s = dest;
  size_t k;

  /* Fill head and tail with minimal branching. Each
   * conditional ensures that all the subsequently used
   * offsets are well-defined and in the dest region. */

  if (!n)
    return dest;
  s[0] = s[n - 1] = c;
  if (n <= 2)
    return dest;
  s[1] = s[n - 2] = c;
  s[2] = s[n - 3] = c;
  if (n <= 6)
    return dest;
  s[3] = s[n - 4] = c;
  if (n <= 8)
    return dest;

  /* Advance pointer to align it at a 4-byte boundary,
   * and truncate n to a multiple of 4. The previous code
   * already took care of any head/tail that get cut off
   * by the alignment. */

  k = -(uintptr_t)s & 3;
  s += k;
  n -= k;
  n &= -4;

  /* Pure C fallback with no aliasing violations. */
  for (; n; n--, s++)
    *s = c;

  return dest;
}

// From //zircon/third_party/ulib/musl/src/string/memcpy.c
void* memcpy(void* restrict dest, const void* restrict src, size_t n) {
  unsigned char* d = dest;
  const unsigned char* s = src;

  for (; n; n--)
    *d++ = *s++;
  return dest;
}
