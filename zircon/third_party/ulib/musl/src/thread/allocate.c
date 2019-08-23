#include <stddef.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "libc.h"
#include "threads_impl.h"
#include "zircon_impl.h"

#if HAVE_SHADOW_CALL_STACK
// TODO(ZX-1947): Add some runtime configuration for the size?  Extra APIs
// would be needed to specify it explicitly.  The only existing API for
// choosing the stack is via pthread_attr_t, which is not something we
// really want to extend since we'd prefer to go to pure C11/C++17 thread
// interfaces rather than POSIX ones.  For SafeStack we didn't add an API
// but just always use the same size for both stacks on the theory that
// virtual memory is cheap if you never actually touch the pages and so
// while you're "allocating" twice as much stack, you're not actually using
// more than one additional page at most by splitting your use between the
// two stacks.  We could do some similar calculation of the appropriate
// ShadowCallStack size for a requested unitary stack size.  But it also
// seems likely that a page is already a lot for the ShadowCallStack since
// that's a call depth of 512 with 64-bit PCs and 4KB pages.
static const size_t kShadowCallStackSize = ZX_PAGE_SIZE;
#else
static const size_t kShadowCallStackSize = 0;
#endif

static pthread_rwlock_t allocation_lock = PTHREAD_RWLOCK_INITIALIZER;

// Many threads could be reading the TLS state.
static void thread_allocation_acquire(void) { pthread_rwlock_rdlock(&allocation_lock); }

// dlopen calls this under another lock. Only one dlopen call can be
// modifying state at a time.
void __thread_allocation_inhibit(void) { pthread_rwlock_wrlock(&allocation_lock); }

void __thread_allocation_release(void) { pthread_rwlock_unlock(&allocation_lock); }

__NO_SAFESTACK static inline size_t round_up_to_page(size_t sz) {
  return (sz + PAGE_SIZE - 1) & -PAGE_SIZE;
}

__NO_SAFESTACK static ptrdiff_t offset_for_module(const struct tls_module* module) {
#ifdef TLS_ABOVE_TP
  return module->offset;
#else
  return -module->offset;
#endif
}

__NO_SAFESTACK static thrd_t copy_tls(unsigned char* mem, size_t alloc) {
  thrd_t td;
  struct tls_module* p;
  size_t i;
  void** dtv;

#ifdef TLS_ABOVE_TP
  // *-----------------------------------------------------------------------*
  // | pthread | tcb | X | tls_1 | ... | tlsN | ... | tls_cnt | dtv[1] | ... |
  // *-----------------------------------------------------------------------*
  // ^         ^         ^             ^            ^
  // td        tp      dtv[1]       dtv[n+1]       dtv
  //
  // Note: The TCB is actually the last member of pthread.
  // See: "Addenda to, and Errata in, the ABI for the ARM Architecture"

  dtv = (void**)(mem + libc.tls_size) - (libc.tls_cnt + 1);
  // We need to make sure that the thread pointer is maximally aligned so
  // that tp + dtv[N] is aligned to align_N no matter what N is. So we need
  // 'mem' to be such that if mem == td then td->head is maximially aligned.
  // To do this we need take &td->head (e.g. mem + offset of head) and align
  // it then subtract out the offset of ->head to ensure that &td->head is
  // aligned.
  uintptr_t tp = (uintptr_t)mem + PTHREAD_TP_OFFSET;
  tp = (tp + libc.tls_align - 1) & -libc.tls_align;
  td = (thrd_t)(tp - PTHREAD_TP_OFFSET);
  // Now mem should be the new thread pointer.
  mem = (unsigned char*)tp;
#else
  // *-----------------------------------------------------------------------*
  // | tls_cnt | dtv[1] | ... | tls_n | ... | tls_1 | tcb | pthread | unused |
  // *-----------------------------------------------------------------------*
  // ^                        ^             ^       ^
  // dtv                   dtv[n+1]       dtv[1]  tp/td
  //
  // Note: The TCB is actually the first member of pthread.
  dtv = (void**)mem;

  mem += alloc - sizeof(struct pthread);
  mem -= (uintptr_t)mem & (libc.tls_align - 1);
  td = (thrd_t)mem;
#endif

  for (i = 1, p = libc.tls_head; p; i++, p = p->next) {
    dtv[i] = mem + offset_for_module(p);
    memcpy(dtv[i], p->image, p->len);
  }

  dtv[0] = (void*)libc.tls_cnt;
  td->head.dtv = dtv;
  return td;
}

__NO_SAFESTACK static bool map_block(zx_handle_t parent_vmar, zx_handle_t vmo, size_t vmo_offset,
                                     size_t size, size_t before, size_t after,
                                     struct iovec* mapping, struct iovec* region) {
  region->iov_len = before + size + after;
  zx_handle_t vmar;
  uintptr_t addr;
  zx_status_t status = _zx_vmar_allocate(
      parent_vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0,
      region->iov_len, &vmar, &addr);
  if (status != ZX_OK)
    return true;
  region->iov_base = (void*)addr;
  status = _zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, before, vmo,
                        vmo_offset, size, &addr);
  if (status != ZX_OK)
    _zx_vmar_destroy(vmar);
  _zx_handle_close(vmar);
  mapping->iov_base = (void*)addr;
  mapping->iov_len = size;
  return status != ZX_OK;
}

// This allocates all the per-thread memory for a new thread about to
// be created, or for the initial thread at startup.  It's called
// either at startup or under thread_allocation_acquire.  Hence,
// it's serialized with any dynamic linker changes to the TLS
// bookkeeping.
//
// This conceptually allocates four things, but concretely allocates
// three separate blocks.
// 1. The safe stack (where the thread's SP will point).
// 2. The unsafe stack (where __builtin___get_unsafe_stack_ptr() will point).
// 3. The thread descriptor (struct pthread).  The thread pointer points
//    into this (where into it depends on the machine ABI).
// 4. The static TLS area.  The ELF TLS ABI for the Initial Exec model
//    mandates a fixed distance from the thread pointer to the TLS area
//    across all threads.  So effectively this must always be allocated
//    as part of the same block with the thread descriptor.
// This function also copies in the TLS initializer data.
// It initializes the basic thread descriptor fields.
// Everything else is zero-initialized.

__NO_SAFESTACK thrd_t __allocate_thread(size_t requested_guard_size, size_t requested_stack_size,
                                        const char* thread_name, char vmo_name[ZX_MAX_NAME_LEN]) {
  // In the initial thread, we're allocating the stacks and TCB for the running
  // thread itself.  So we can't make calls that rely on safe-stack or
  // shadow-call-stack setup.  Rather than annotating everything in the call
  // path here, we just avoid the problematic calls.  Locking is not required
  // since this is the sole thread.
  const bool initial_thread = vmo_name == NULL;

  if (!initial_thread) {
    thread_allocation_acquire();
  }

  const size_t guard_size = requested_guard_size == 0 ? 0 : round_up_to_page(requested_guard_size);
  const size_t stack_size = round_up_to_page(requested_stack_size);

  const size_t tls_size = libc.tls_size;
  const size_t tcb_size = round_up_to_page(tls_size);

  const size_t vmo_size = tcb_size + stack_size * 2 + kShadowCallStackSize;
  zx_handle_t vmo;
  zx_status_t status = _zx_vmo_create(vmo_size, 0, &vmo);
  if (status != ZX_OK) {
    if (!initial_thread) {
      __thread_allocation_release();
    }
    return NULL;
  }
  struct iovec tcb, tcb_region;
  if (map_block(_zx_vmar_root_self(), vmo, 0, tcb_size, PAGE_SIZE, PAGE_SIZE, &tcb, &tcb_region)) {
    if (!initial_thread) {
      __thread_allocation_release();
    }
    _zx_handle_close(vmo);
    return NULL;
  }

  thrd_t td = copy_tls(tcb.iov_base, tcb.iov_len);

  // At this point all our access to global TLS state is done, so we
  // can allow dlopen again.
  if (!initial_thread) {
    __thread_allocation_release();
  }

  // For the initial thread, it's too early to call snprintf because
  // it's not __NO_SAFESTACK.
  if (!initial_thread) {
    // For other threads, try to give the VMO a name that includes
    // the thrd_t value (and the TLS size if that fits too), but
    // don't use a truncated value since that would be confusing to
    // interpret.
    if (snprintf(vmo_name, ZX_MAX_NAME_LEN, "%s:%p/TLS=%#zx", thread_name, td, tls_size) <
            ZX_MAX_NAME_LEN ||
        snprintf(vmo_name, ZX_MAX_NAME_LEN, "%s:%p", thread_name, td) < ZX_MAX_NAME_LEN)
      thread_name = vmo_name;
  }
  _zx_object_set_property(vmo, ZX_PROP_NAME, thread_name, strlen(thread_name));

  if (map_block(_zx_vmar_root_self(), vmo, tcb_size, stack_size, guard_size, 0, &td->safe_stack,
                &td->safe_stack_region)) {
    _zx_vmar_unmap(_zx_vmar_root_self(), (uintptr_t)tcb_region.iov_base, tcb_region.iov_len);
    _zx_handle_close(vmo);
    return NULL;
  }

  if (map_block(_zx_vmar_root_self(), vmo, tcb_size + stack_size, stack_size, guard_size, 0,
                &td->unsafe_stack, &td->unsafe_stack_region)) {
    _zx_vmar_unmap(_zx_vmar_root_self(), (uintptr_t)td->safe_stack_region.iov_base,
                   td->safe_stack_region.iov_len);
    _zx_vmar_unmap(_zx_vmar_root_self(), (uintptr_t)tcb_region.iov_base, tcb_region.iov_len);
    _zx_handle_close(vmo);
    return NULL;
  }

#if HAVE_SHADOW_CALL_STACK
  if (map_block(_zx_vmar_root_self(), vmo, tcb_size + stack_size * 2,
                // Shadow call stack grows up, so a guard after is probably
                // enough.  But be extra careful with guards on both sides.
                kShadowCallStackSize, guard_size, guard_size,
                //
                &td->shadow_call_stack, &td->shadow_call_stack_region)) {
    _zx_vmar_unmap(_zx_vmar_root_self(), (uintptr_t)td->unsafe_stack_region.iov_base,
                   td->unsafe_stack_region.iov_len);
    _zx_vmar_unmap(_zx_vmar_root_self(), (uintptr_t)td->safe_stack_region.iov_base,
                   td->safe_stack_region.iov_len);
    _zx_vmar_unmap(_zx_vmar_root_self(), (uintptr_t)tcb_region.iov_base, tcb_region.iov_len);
    _zx_handle_close(vmo);
    return NULL;
  }
#endif

  _zx_handle_close(vmo);
  td->tcb_region = tcb_region;
  td->locale = &libc.global_locale;
  td->head.tp = (uintptr_t)pthread_to_tp(td);
  td->abi.stack_guard = __stack_chk_guard;
  td->abi.unsafe_sp = (uintptr_t)td->unsafe_stack.iov_base + td->unsafe_stack.iov_len;
  return td;
}
