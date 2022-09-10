// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/machine.h>
#include <lib/stdcompat/atomic.h>
#include <lib/zircon-internal/unique-backtrace.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/syscalls.h>

#include <runtime/thread.h>

// An zxr_thread_t starts its life JOINABLE.
// - If someone calls zxr_thread_join on it, it transitions to JOINED.
// - If someone calls zxr_thread_detach on it, it transitions to DETACHED.
// - When it begins exiting, the EXITING state is entered.
// - When it is no longer using its memory and handle resources, it transitions
//   to DONE.  If the thread was DETACHED prior to EXITING, this transition MAY
//   not happen.
// No other transitions occur.

enum {
  JOINABLE,
  DETACHED,
  JOINED,
  EXITING,
  DONE,
  FREED,
};

union zxr_internal_thread_t {
  cpp20::atomic_ref<int> atomic_state() { return cpp20::atomic_ref(state); }

  zxr_thread_t external;
  struct {
    zxr_thread_entry_t entry;
    zx_handle_t handle;
    int state;  // Only accessed via atomic_ref.
  };
};

// zxr_thread_t should reserve enough size for our internal data.
static_assert(sizeof(zxr_thread_t) == sizeof(zxr_internal_thread_t),
              "Update zxr_thread_t size for this platform.");

static inline zxr_internal_thread_t* to_internal(zxr_thread_t* external) {
  return reinterpret_cast<zxr_internal_thread_t*>(external);
}

zx_status_t zxr_thread_destroy(zxr_thread_t* thread) {
  const zx_handle_t handle = to_internal(thread)->handle;
  to_internal(thread)->handle = ZX_HANDLE_INVALID;
  return handle == ZX_HANDLE_INVALID ? ZX_OK : _zx_handle_close(handle);
}

// Put the thread into EXITING state.  Returns the previous state.
static int begin_exit(zxr_internal_thread_t* thread) {
  return thread->atomic_state().exchange(EXITING, std::memory_order_release);
}

// Claim the thread as JOINED or DETACHED.  Returns true on success, which only
// happens if the previous state was JOINABLE.  Always returns the previous state.
static bool claim_thread(zxr_internal_thread_t* thread, int new_state, int* old_state) {
  *old_state = JOINABLE;
  return thread->atomic_state().compare_exchange_strong(
      *old_state, new_state, std::memory_order_acq_rel, std::memory_order_acquire);
}

// Extract the handle from the thread structure. Synchronizes with readers by
// setting the state to FREED and checks the given expected state for consistency.
static zx_handle_t take_handle(zxr_internal_thread_t* thread, int expected_state) {
  const zx_handle_t tmp = thread->handle;
  thread->handle = ZX_HANDLE_INVALID;

  if (!thread->atomic_state().compare_exchange_strong(
          expected_state, FREED, std::memory_order_acq_rel, std::memory_order_acquire)) {
    CRASH_WITH_UNIQUE_BACKTRACE();
  }

  return tmp;
}

static _Noreturn void exit_non_detached(zxr_internal_thread_t* thread) {
  // Wake the _zx_futex_wait in zxr_thread_join (below), and then die.
  // This has to be done with the special four-in-one vDSO call because
  // as soon as the state transitions to DONE, the joiner is free to unmap
  // our stack out from under us.  Note there is a benign race here still: if
  // the address is unmapped and our futex_wake fails, it's OK; if the memory
  // is reused for something else and our futex_wake tickles somebody
  // completely unrelated, well, that's why futex_wait can always have
  // spurious wakeups.
  _zx_futex_wake_handle_close_thread_exit(&thread->state, 1, DONE, ZX_HANDLE_INVALID);
  CRASH_WITH_UNIQUE_BACKTRACE();
}

static _Noreturn void thread_trampoline(uintptr_t ctx, uintptr_t arg) {
  zxr_internal_thread_t* thread = reinterpret_cast<zxr_internal_thread_t*>(ctx);

  thread->entry(reinterpret_cast<void*>(arg));

  const int old_state = begin_exit(thread);
  switch (old_state) {
    case JOINABLE:
      // Nobody's watching right now, but they might start watching as we
      // exit.  Just in case, behave as if we've been joined and wake the
      // futex on our way out.
    case JOINED:
      // Somebody loves us!  Or at least intends to inherit when we die.
      exit_non_detached(thread);
      break;
  }

  // Cannot be in DONE, EXITING, or DETACHED and reach here.  For DETACHED, it
  // is the responsibility of a higher layer to ensure this is not reached.
  CRASH_WITH_UNIQUE_BACKTRACE();
}

_Noreturn void zxr_thread_exit_unmap_if_detached(zxr_thread_t* thread, void (*if_detached)(void*),
                                                 void* if_detached_arg,

                                                 zx_handle_t vmar, uintptr_t addr, size_t len) {
  const int old_state = begin_exit(to_internal(thread));
  switch (old_state) {
    case DETACHED: {
      (*if_detached)(if_detached_arg);
      const zx_handle_t handle = take_handle(to_internal(thread), EXITING);
      _zx_vmar_unmap_handle_close_thread_exit(vmar, addr, len, handle);
      break;
    }
    // See comments in thread_trampoline.
    case JOINABLE:
    case JOINED:
      exit_non_detached(to_internal(thread));
      break;
  }

  // Cannot be in DONE or the EXITING and reach here.
  CRASH_WITH_UNIQUE_BACKTRACE();
}

// Local implementation so libruntime does not depend on libc.
static size_t local_strlen(const char* s) {
  size_t len = 0;
  while (*s++ != '\0') {
    ++len;
  }
  return len;
}

static void initialize_thread(zxr_internal_thread_t* thread, zx_handle_t handle, bool detached) {
  *thread = zxr_internal_thread_t{.handle = handle};
  thread->atomic_state().store(detached ? DETACHED : JOINABLE, std::memory_order_release);
}

zx_status_t zxr_thread_create(zx_handle_t process, const char* name, bool detached,
                              zxr_thread_t* thread) {
  initialize_thread(to_internal(thread), ZX_HANDLE_INVALID, detached);

  if (name == nullptr) {
    name = "";
  }
  const size_t name_length = local_strlen(name) + 1;

  return _zx_thread_create(process, name, name_length, 0, &to_internal(thread)->handle);
}

zx_status_t zxr_thread_start(zxr_thread_t* thread, uintptr_t stack_addr, size_t stack_size,
                             zxr_thread_entry_t entry, void* arg) {
  to_internal(thread)->entry = entry;

  // compute the starting address of the stack
  const uintptr_t sp = elfldltl::AbiTraits<>::InitialStackPointer(stack_addr, stack_size);

  // kick off the new thread
  const zx_status_t status =
      _zx_thread_start(to_internal(thread)->handle, reinterpret_cast<uintptr_t>(thread_trampoline),
                       sp, reinterpret_cast<uintptr_t>(thread), reinterpret_cast<uintptr_t>(arg));
  if (status != ZX_OK) {
    zxr_thread_destroy(thread);
  }

  return status;
}

static void wait_for_done(zxr_internal_thread_t* thread, int32_t old_state) {
  do {
    switch (_zx_futex_wait(&thread->state, old_state, ZX_HANDLE_INVALID, ZX_TIME_INFINITE)) {
      case ZX_ERR_BAD_STATE:  // Never blocked because it had changed.
      case ZX_OK:             // Woke up because it might have changed.
        old_state = thread->atomic_state().load(std::memory_order_acquire);
        break;
      default:
        CRASH_WITH_UNIQUE_BACKTRACE();
    }
    // Wait until we reach the DONE state, even if we observe the
    // intermediate EXITING state.
  } while (old_state == JOINED || old_state == EXITING);

  if (old_state != DONE)
    CRASH_WITH_UNIQUE_BACKTRACE();
}

zx_status_t zxr_thread_join(zxr_thread_t* external_thread) {
  zxr_internal_thread_t* thread = to_internal(external_thread);

  int old_state;
  // Try to claim the join slot on this thread.
  if (claim_thread(thread, JOINED, &old_state)) {
    wait_for_done(thread, JOINED);
  } else {
    switch (old_state) {
      case JOINED:
      case DETACHED:
        return ZX_ERR_INVALID_ARGS;
      case EXITING:
        // Since it is undefined to call zxr_thread_join on a thread
        // that has already been detached or joined, we assume the state
        // prior to EXITING was JOINABLE, and act as if we had
        // successfully transitioned to JOINED.
        wait_for_done(thread, EXITING);
        __FALLTHROUGH;
      case DONE:
        break;
      default:
        CRASH_WITH_UNIQUE_BACKTRACE();
    }
  }

  // Take the handle and synchronize with readers.
  const zx_handle_t handle = take_handle(thread, DONE);
  if (handle == ZX_HANDLE_INVALID || zx_handle_close(handle) != ZX_OK) {
    CRASH_WITH_UNIQUE_BACKTRACE();
  }

  return ZX_OK;
}

zx_status_t zxr_thread_detach(zxr_thread_t* thread) {
  int old_state;
  // Try to claim the join slot on this thread on behalf of the thread.
  if (!claim_thread(to_internal(thread), DETACHED, &old_state)) {
    switch (old_state) {
      case DETACHED:
      case JOINED:
        return ZX_ERR_INVALID_ARGS;
      case EXITING: {
        // Since it is undefined behavior to call zxr_thread_detach on a
        // thread that has already been detached or joined, we assume
        // the state prior to EXITING was JOINABLE.  However, since the
        // thread is already shutting down, it is too late to tell it to
        // clean itself up.  Since the thread is still running, we cannot
        // just return ZX_ERR_BAD_STATE, which would suggest we couldn't detach and
        // the thread has already finished running.  Instead, we call join,
        // which will return soon due to the thread being actively shutting down,
        // and then return ZX_ERR_BAD_STATE to tell the caller that they
        // must manually perform any post-join work.
        const zx_status_t ret = zxr_thread_join(thread);
        if (unlikely(ret != ZX_OK)) {
          if (unlikely(ret != ZX_ERR_INVALID_ARGS)) {
            CRASH_WITH_UNIQUE_BACKTRACE();
          }
          return ret;
        }
      }
        // Fall-through to DONE case.
        __FALLTHROUGH;
      case DONE:
        return ZX_ERR_BAD_STATE;
      default:
        CRASH_WITH_UNIQUE_BACKTRACE();
    }
  }

  return ZX_OK;
}

bool zxr_thread_detached(zxr_thread_t* thread) {
  const int state = to_internal(thread)->atomic_state().load(std::memory_order_acquire);
  return state == DETACHED;
}

zx_handle_t zxr_thread_get_handle(zxr_thread_t* thread) {
  // Synchronize with writers before reading handle.
  const int state = to_internal(thread)->atomic_state().load(std::memory_order_acquire);
  (void)state;
  return to_internal(thread)->handle;
}

zx_status_t zxr_thread_adopt(zx_handle_t handle, zxr_thread_t* thread) {
  initialize_thread(to_internal(thread), handle, false);
  return handle == ZX_HANDLE_INVALID ? ZX_ERR_BAD_HANDLE : ZX_OK;
}
