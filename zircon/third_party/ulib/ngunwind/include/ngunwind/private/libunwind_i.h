/* libunwind - a platform-independent unwind library
   Copyright (C) 2001-2005 Hewlett-Packard Co
   Copyright (C) 2007 David Mosberger-Tang
        Contributed by David Mosberger-Tang <dmosberger@gmail.com>

This file is part of libunwind.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

/* This files contains libunwind-internal definitions which are
   subject to frequent change and are not to be exposed to
   libunwind-users.  */

#ifndef libunwind_i_h
#define libunwind_i_h

#include "config.h"

/* Platform-independent libunwind-internal declarations.  */

#include <assert.h>
#include <elf.h>
#include <endian.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include <ngunwind/libunwind.h>
#include <ngunwind/dwarf.h>

#include "compiler.h"

#ifdef DEBUG
# define UNW_DEBUG      1
#else
# define UNW_DEBUG      0
#endif

/* Make it easy to write thread-safe code which may or may not be
   linked against libpthread.  The macros below can be used
   unconditionally and if -lpthread is around, they'll call the
   corresponding routines otherwise, they do nothing.  */

#pragma weak pthread_mutex_init
#pragma weak pthread_mutex_lock
#pragma weak pthread_mutex_unlock

#define mutex_init(l)                                                   \
        (pthread_mutex_init != NULL ? pthread_mutex_init ((l), NULL) : 0)
#define mutex_lock(l)                                                   \
        (pthread_mutex_lock != NULL ? pthread_mutex_lock (l) : 0)
#define mutex_unlock(l)                                                 \
        (pthread_mutex_unlock != NULL ? pthread_mutex_unlock (l) : 0)

#ifdef HAVE_ATOMIC_OPS_H
# include <atomic_ops.h>
static inline int
cmpxchg_ptr (void *addr, void *old_value, void *new_value)
{
  union
    {
      void *vp;
      AO_t *aop;
    }
  u;

  u.vp = addr;
  return AO_compare_and_swap(u.aop, (AO_t) old_value, (AO_t) new_value);
}
# define fetch_and_add1(_ptr)           AO_fetch_and_add1(_ptr)
# define fetch_and_add(_ptr, value)     AO_fetch_and_add(_ptr, value)
# define HAVE_CMPXCHG
# define HAVE_FETCH_AND_ADD
#elif defined(HAVE_SYNC_ATOMICS)
static inline int
cmpxchg_ptr (void *addr, void *old_value, void *new_value)
{
  union
    {
      void *vp;
      long *vlp;
    }
  u;

  u.vp = addr;
  return __sync_bool_compare_and_swap(u.vlp, (long) old_value, (long) new_value);
}
# define fetch_and_add1(_ptr)           __sync_fetch_and_add(_ptr, 1)
# define fetch_and_add(_ptr, value)     __sync_fetch_and_add(_ptr, value)
# define HAVE_CMPXCHG
# define HAVE_FETCH_AND_ADD
#endif
#define atomic_read(ptr)        (*(ptr))

/* Type of a mask that can be used to inhibit preemption.  At the
   userlevel, preemption is caused by signals and hence sigset_t is
   appropriate.  In constrast, the Linux kernel uses "unsigned long"
   to hold the processor "flags" instead.  */
typedef sigset_t intrmask_t;

extern intrmask_t unwi_full_mask;

/* Silence compiler warnings about variables which are used only if libunwind
   is configured in a certain way */
static inline void mark_as_used(void *v UNUSED) {
}

#if defined(CONFIG_BLOCK_SIGNALS)
# define SIGPROCMASK(how, new_mask, old_mask) \
  sigprocmask((how), (new_mask), (old_mask))
#else
# define SIGPROCMASK(how, new_mask, old_mask) mark_as_used(old_mask)
#endif

/* Prefer adaptive mutexes if available */
#ifdef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
#define UNW_PTHREAD_MUTEX_INITIALIZER PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
#else
#define UNW_PTHREAD_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#endif

#define define_lock(name) \
  pthread_mutex_t name = UNW_PTHREAD_MUTEX_INITIALIZER
#define lock_init(l)            mutex_init (l)
#define lock_acquire(l,m)                               \
do {                                                    \
  SIGPROCMASK (SIG_SETMASK, &unwi_full_mask, &(m));     \
  mutex_lock (l);                                       \
} while (0)
#define lock_release(l,m)                       \
do {                                            \
  mutex_unlock (l);                             \
  SIGPROCMASK (SIG_SETMASK, &(m), NULL);        \
} while (0)

#define SOS_MEMORY_SIZE 16384   /* see src/mi/mempool.c */

#ifndef MAP_ANONYMOUS
# define MAP_ANONYMOUS MAP_ANON
#endif
#define GET_MEMORY(mem, size)                                               \
do {                                                                        \
  /* Hopefully, mmap() goes straight through to a system call stub...  */   \
  mem = mmap (NULL, size, PROT_READ | PROT_WRITE,                           \
              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);                          \
  if (mem == MAP_FAILED)                                                    \
    mem = NULL;                                                             \
} while (0)

extern int unwi_find_dynamic_proc_info (unw_addr_space_t as,
                                        unw_word_t ip,
                                        unw_proc_info_t *pi,
                                        int need_unwind_info, void *arg);
extern int unwi_extract_dynamic_proc_info (unw_addr_space_t as,
                                           unw_word_t ip,
                                           unw_proc_info_t *pi,
                                           unw_dyn_info_t *di,
                                           int need_unwind_info,
                                           void *arg);
extern void unwi_put_dynamic_unwind_info (unw_addr_space_t as,
                                          unw_proc_info_t *pi, void *arg);

/* These handle the remote (cross-address-space) case of accessing
   dynamic unwind info. */

extern int unwi_dyn_remote_find_proc_info (unw_addr_space_t as,
                                           unw_word_t ip,
                                           unw_proc_info_t *pi,
                                           int need_unwind_info,
                                           void *arg);
extern void unwi_dyn_remote_put_unwind_info (unw_addr_space_t as,
                                             unw_proc_info_t *pi,
                                             void *arg);
extern int unwi_dyn_validate_cache (unw_addr_space_t as, void *arg);

extern unw_dyn_info_list_t _U_dyn_info_list;
extern pthread_mutex_t _U_dyn_info_list_lock;

#if UNW_DEBUG
extern int unwi_debug_level;

# include <stdio.h>
# define Debug(level,format...)                                         \
do {                                                                    \
  if (unwi_debug_level >= level)                                        \
    {                                                                   \
      int _n = level;                                                   \
      if (_n > 16)                                                      \
        _n = 16;                                                        \
      fprintf (stderr, "%*c>%s: ", _n, ' ', __FUNCTION__);              \
      fprintf (stderr, format);                                         \
    }                                                                   \
} while (0)
# define Dprintf(format...)         fprintf (stderr, format)
#else
# define Debug(level,format...)
# define Dprintf(format...)
#endif

extern int unwi_print_error (const char *string);

extern void mi_init (void);     /* machine-independent initializations */
extern unw_word_t _U_dyn_info_list_addr (void);

/* This is needed/used by ELF targets only.  */

struct elf_image
  {
    void *image;                /* pointer to mmap'd image */
    size_t size;                /* (file-) size of the image */
  };

struct elf_dyn_info
  {
    struct elf_image ei;
    unw_dyn_info_t di_cache;
    unw_dyn_info_t di_debug;    /* additional table info for .debug_frame */
#if UNW_TARGET_ARM
    unw_dyn_info_t di_arm;      /* additional table info for .ARM.exidx */
#endif
  };

// as: Address Space

struct as_contents
  {
    void *data;
    size_t size;
  };

struct as_elf_dyn_info
  {
    void* arg;                  /* the "arg" to address space accessors */
    unw_dyn_info_t di_cache;
    unw_dyn_info_t di_debug;    /* additional table info for .debug_frame */

    /* loaded contents */
    struct as_contents ehdr;
    struct as_contents phdr;
    struct as_contents eh;
    struct as_contents dyn;
  };

extern void unwi_invalidate_edi (struct elf_dyn_info *edi);

extern void unwi_invalidate_as_edi (struct as_elf_dyn_info *edi);

extern int unwi_load_as_contents (unw_addr_space_t as, struct as_contents *contents,
                                  unw_word_t offset, size_t size, void *arg);

/* Define GNU and processor specific values for the Phdr p_type field in case
   they aren't defined by <elf.h>.  */
#ifndef PT_GNU_EH_FRAME
# define PT_GNU_EH_FRAME        0x6474e550
#endif /* !PT_GNU_EH_FRAME */
#ifndef PT_ARM_EXIDX
# define PT_ARM_EXIDX           0x70000001      /* ARM unwind segment */
#endif /* !PT_ARM_EXIDX */

struct unw_addr_space
  {
    struct unw_accessors acc;
    int big_endian;
    unw_caching_policy_t caching_policy;
#ifdef HAVE_ATOMIC_OPS_H
    AO_t cache_generation;
#else
    uint32_t cache_generation;
#endif
    unw_word_t dyn_generation;          /* see dyn-common.h */
    unw_word_t dyn_info_list_addr;      /* (cached) dyn_info_list_addr */
    struct dwarf_rs_cache global_cache;
    struct unw_debug_frame_list *debug_frames;
  };

typedef struct
  {
    unw_word_t virtual_address;
    int32_t frame_type : 2; /* unw_tdep_frame_type_t classification */
    int32_t last_frame : 1; /* non-zero if last frame in chain */
    int32_t cfa_reg_sp : 1; /* cfa dwarf base register is sp vs. fp */
    int32_t cfa_reg_offset; /* cfa is at this offset from base register value */
    int32_t fp_cfa_offset;  /* fp saved at this offset from cfa (-1 = not saved) */
    int32_t sp_cfa_offset;  /* sp saved at this offset from cfa (-1 = not saved) */
#if UNW_TARGET_ARM || UNW_TARGET_AARCH64
    int32_t lr_cfa_offset;  /* lr saved at this offset from cfa (-1 = not saved) */
#endif
  }
unw_tdep_frame_t;

#if defined __arm__
# include "tgt_i-arm.h"
#elif defined __aarch64__
# include "tgt_i-aarch64.h"
#elif defined __x86_64__
# include "tgt_i-x86_64.h"
#elif defined __riscv
# include "tgt_i-riscv64.h"
#else
# error "Unsupported arch"
#endif

struct cursor
  {
    struct dwarf_cursor dwarf;          /* must be first */

    unw_tdep_frame_t frame_info;        /* quick tracing assist info */

    /* Format of sigcontext structure and address at which it is stored: */
    unw_tdep_sigcontext_format_t sigcontext_format;
    unw_word_t sigcontext_addr;

    int validate;
    ucontext_t *uc;
  };

/* Platforms that support UNW_INFO_FORMAT_TABLE need to define
   tdep_search_unwind_table.  */
#if !UNW_TARGET_ARM /* arm has its own version */
#define tdep_search_unwind_table dwarf_search_unwind_table
#endif

#define tdep_find_unwind_table dwarf_find_unwind_table

// These allow target-specific frame handling.
// Linux uses this for signal frame handling.
#if UNW_TARGET_X86_64 && defined (__linux__)
extern void tdep_fetch_frame (struct dwarf_cursor *c, unw_word_t ip,
                              int need_unwind_info);
extern void tdep_cache_frame (struct dwarf_cursor *c,
                              struct dwarf_reg_state *rs);
extern void tdep_reuse_frame (struct dwarf_cursor *c,
                              struct dwarf_reg_state *rs);
#else
#define tdep_fetch_frame(c,ip,n) do {} while(0)
#define tdep_cache_frame(c,rs)   do {} while(0)
#define tdep_reuse_frame(c,rs)   do {} while(0)
#endif

extern void tdep_stash_frame (struct dwarf_cursor *c,
                              struct dwarf_reg_state *rs);

#define tdep_find_proc_info(c,ip,n) \
  (*(c)->as->acc.find_proc_info) ((c)->as, (ip), &(c)->pi, (n), \
                                  (c)->as_arg)
#define tdep_put_unwind_info(as,pi,arg) \
  (*(as)->acc.put_unwind_info) ((as), (pi), (arg))

#define tdep_get_as(c)      ((c)->dwarf.as)
#define tdep_get_as_arg(c)  ((c)->dwarf.as_arg)
#define tdep_get_ip(c)      ((c)->dwarf.ip)
#define tdep_big_endian(as) ((as)->big_endian)

extern int tdep_init_done;

extern void tdep_init (void);
extern void tdep_init_mem_validate (void);
extern int tdep_search_unwind_table (unw_addr_space_t as, unw_word_t ip,
                                     unw_dyn_info_t *di, unw_proc_info_t *pi,
                                     int need_unwind_info, void *arg);
extern int tdep_get_elf_image (struct elf_image *ei, pid_t pid, unw_word_t ip,
                               unsigned long *segbase, unsigned long *mapoff,
                               char *path, size_t pathlen);
extern int tdep_access_reg (struct cursor *c, unw_regnum_t reg,
                            unw_word_t *valp, int write);
extern int tdep_access_fpreg (struct cursor *c, unw_regnum_t reg,
                              unw_fpreg_t *valp, int write);

#ifndef tdep_get_func_addr
# define tdep_get_func_addr(as,addr,v)          (*(v) = addr, 0)
#endif

#define UNW_ALIGN(x,a) (((x)+(a)-1UL)&~((a)-1UL))

#define ARRAY_SIZE(a)   (sizeof (a) / sizeof ((a)[0]))

#endif /* libunwind_i_h */
