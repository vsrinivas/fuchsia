// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <bits.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/ioport.h>
#include <arch/x86/mp.h>
#include <fbl/alloc_checker.h>
#include <kernel/auto_lock.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <ktl/move.h>
#include <ktl/unique_ptr.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>

void x86_reset_tss_io_bitmap(void) {
  DEBUG_ASSERT(arch_ints_disabled());
  tss_t* tss = &x86_get_percpu()->default_tss;
  auto tss_bitmap = reinterpret_cast<unsigned long*>(tss->tss_bitmap);

  bitmap_set(tss_bitmap, 0, IO_BITMAP_BITS);
}

static void x86_clear_tss_io_bitmap(const bitmap::RleBitmap& bitmap) {
  DEBUG_ASSERT(arch_ints_disabled());
  tss_t* tss = &x86_get_percpu()->default_tss;

  auto tss_bitmap = reinterpret_cast<unsigned long*>(tss->tss_bitmap);
  for (const auto& extent : bitmap) {
    DEBUG_ASSERT(extent.bitoff + extent.bitlen <= IO_BITMAP_BITS);
    bitmap_set(tss_bitmap, static_cast<int>(extent.bitoff), static_cast<int>(extent.bitlen));
  }
}

void x86_clear_tss_io_bitmap(IoBitmap& io_bitmap) {
  AutoSpinLockNoIrqSave guard(&io_bitmap.lock_);
  if (!io_bitmap.bitmap_)
    return;

  x86_clear_tss_io_bitmap(*io_bitmap.bitmap_);
}

static void x86_set_tss_io_bitmap(const bitmap::RleBitmap& bitmap) {
  DEBUG_ASSERT(arch_ints_disabled());
  tss_t* tss = &x86_get_percpu()->default_tss;

  auto tss_bitmap = reinterpret_cast<unsigned long*>(tss->tss_bitmap);
  for (const auto& extent : bitmap) {
    DEBUG_ASSERT(extent.bitoff + extent.bitlen <= IO_BITMAP_BITS);
    bitmap_clear(tss_bitmap, static_cast<int>(extent.bitoff), static_cast<int>(extent.bitlen));
  }
}

void x86_set_tss_io_bitmap(IoBitmap& io_bitmap) {
  AutoSpinLockNoIrqSave guard(&io_bitmap.lock_);
  if (!io_bitmap.bitmap_)
    return;

  x86_set_tss_io_bitmap(*io_bitmap.bitmap_);
}

IoBitmap* IoBitmap::GetCurrent() {
  // Fetch current thread's address space. If we have no address space (e.g.,
  // the idle thread), we also don't have an IO Bitmap.
  VmAspace* aspace = Thread::Current::Get()->aspace();
  if (aspace == nullptr) {
    return nullptr;
  }

  return &aspace->arch_aspace().io_bitmap();
}

IoBitmap::~IoBitmap() {}

struct ioport_update_context {
  // IoBitmap that we're trying to update
  IoBitmap* io_bitmap;
};

void IoBitmap::UpdateTask(void* raw_context) {
  DEBUG_ASSERT(arch_ints_disabled());
  struct ioport_update_context* context = (struct ioport_update_context*)raw_context;
  DEBUG_ASSERT(context->io_bitmap != nullptr);

  // If our CPU's active bitmap matches the one that has been updated,
  // reprogram the hardware to match.
  IoBitmap* io_bitmap = GetCurrent();
  if (io_bitmap == context->io_bitmap) {
    AutoSpinLockNoIrqSave guard(&io_bitmap->lock_);
    // This is overkill, but it's much simpler to reason about
    x86_reset_tss_io_bitmap();
    x86_set_tss_io_bitmap(*io_bitmap->bitmap_);
  }
}

int IoBitmap::SetIoBitmap(uint32_t port, uint32_t len, bool enable) {
  DEBUG_ASSERT(!arch_ints_disabled());

  if ((port + len < port) || (port + len > IO_BITMAP_BITS))
    return ZX_ERR_INVALID_ARGS;

  ktl::unique_ptr<bitmap::RleBitmap> optimistic_bitmap;
  if (!bitmap_) {
    // Optimistically allocate a bitmap structure if we don't have one, and
    // we'll see if we actually need this allocation later.  In the common
    // case, when we make the allocation we will use it.
    fbl::AllocChecker ac;
    optimistic_bitmap.reset(new (&ac) bitmap::RleBitmap());
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  // Create a free-list in case any of our bitmap operations need to free any
  // nodes.
  bitmap::RleBitmap::FreeList bitmap_freelist;

  // Optimistically allocate an element for the bitmap, in case we need one.
  {
    fbl::AllocChecker ac;
    bitmap_freelist.push_back(
        ktl::unique_ptr<bitmap::RleBitmapElement>(new (&ac) bitmap::RleBitmapElement()));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  // Update this thread's bitmap.
  //
  // Keep in mind there are really two bitmaps, this thread's bitmap (|bitmap_|)
  // and the one *in* the CPU on which this thread is executing.  The procedure
  // for updating |bitmap_| is security critical.
  //
  // During a context switch, the in-CPU bitmap is adjusted using both the old
  // thread's |bitmap_| and the new thread's |bitmap_|.  The bits that were set
  // in old thread's |bitmap_| are cleared from the in-CPU state and the bits
  // that are set in the new thread's |bitmap_| are set in the in-CPU state.
  //
  // At the time of context switch, it is crucial that the old thread's
  // |bitmap_| match the in-CPU state.  Otherwise, the context switch may fail
  // to clear some bits and inadvertently grant the new thread elevated
  // privilege.
  //
  // One we have modified |bitmap_| we must ensure that no other thread executes
  // on this CPU until the in-CPU state has been updated.  To accomplish that,
  // we disable preemption and take care to not call any functions that might
  // block or otherwise enter the scheduler.
  {
    AutoPreemptDisabler<APDInitialState::PREEMPT_DISABLED> preempt_disabler;

    {
      AutoSpinLock guard(&lock_);

      if (!bitmap_) {
        bitmap_ = ktl::move(optimistic_bitmap);
      }
      DEBUG_ASSERT(bitmap_);

      zx_status_t status = enable ? bitmap_->SetNoAlloc(port, port + len, &bitmap_freelist)
                                  : bitmap_->ClearNoAlloc(port, port + len, &bitmap_freelist);
      if (status != ZX_OK) {
        return status;
      }
    }

    // Let all CPUs know about the update.
    struct ioport_update_context task_context = {.io_bitmap = this};
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, IoBitmap::UpdateTask, &task_context);

    // Now that we've returned from |mp_sync_exec|, we know this CPU's state
    // matches the updated |bitmap_|.  It's now safe to re-enable preemption.
  }

  return ZX_OK;
}
