#ifndef SYSROOT_LINK_H_
#define SYSROOT_LINK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <elf.h>
#define __NEED_size_t
#define __NEED_uint32_t
#include <bits/alltypes.h>

#define ElfW(type) Elf64_##type

/* this is the same everywhere except alpha and s390 */
typedef uint32_t Elf_Symndx;

struct dl_phdr_info {
  ElfW(Addr) dlpi_addr;
  const char* dlpi_name;
  const ElfW(Phdr) * dlpi_phdr;
  ElfW(Half) dlpi_phnum;
  unsigned long long int dlpi_adds;
  unsigned long long int dlpi_subs;
  size_t dlpi_tls_modid;
  void* dlpi_tls_data;
};

struct link_map {
  ElfW(Addr) l_addr;
  char* l_name;
  ElfW(Dyn) * l_ld;
  struct link_map *l_next, *l_prev;
};

struct r_debug {
  int r_version;
  struct link_map* r_map;
  ElfW(Addr) r_brk;

  /* This is the address of a function internal to the run-time linker
     that triggers a debug trap. This function will always be called
     when the linker begins to map in a library or unmap it, and again
     when the mapping change is complete.

     The debugger can compare the address of a sw exception to this value
     to determine whether the debug trap was triggered by the run-time
     linker. */
  ElfW(Addr) r_brk_on_load;

  enum { RT_CONSISTENT, RT_ADD, RT_DELETE } r_state;
  ElfW(Addr) r_ldbase;
};

int dl_iterate_phdr(int (*)(struct dl_phdr_info*, size_t, void*), void*);

#ifdef __cplusplus
}
#endif

#endif  // SYSROOT_LINK_H_
