// Copyright 2019 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_PC_INTERRUPT_MANAGER_H_
#define ZIRCON_KERNEL_PLATFORM_PC_INTERRUPT_MANAGER_H_

#include <align.h>
#include <err.h>
#include <pow2.h>
#include <trace.h>

#include <arch/x86/apic.h>
#include <arch/x86/interrupts.h>
#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <dev/interrupt.h>
#include <kernel/lockdep.h>
#include <kernel/spinlock.h>

#define MAX_IRQ_BLOCK_SIZE MAX_MSI_IRQS

// PC implementation of interrupt management.  This is templated on an IoApic
// implementation to allow for mocking it out during tests.
template <typename IoApic>
class InterruptManager {
 public:
  InterruptManager() = default;
  ~InterruptManager() = default;
  InterruptManager(const InterruptManager&) = delete;
  InterruptManager(InterruptManager&&) = delete;
  InterruptManager& operator=(InterruptManager&&) = delete;
  InterruptManager& operator=(const InterruptManager&) = delete;

  static constexpr unsigned int kNumCpuVectors = X86_INT_PLATFORM_MAX - X86_INT_PLATFORM_BASE + 1;

  // Initialize the x86 IRQ vector allocator and add the range of vectors to manage.
  zx_status_t Init() {
    // This is a statically allocated vector so reset should not fail.
    Guard<SpinLock, IrqSave> guard{&lock_};
    return handler_allocated_.Reset(X86_INT_COUNT);
  }

  zx_status_t MaskInterrupt(unsigned int vector) {
    Guard<SpinLock, IrqSave> guard{&lock_};
    IoApic::MaskIrq(vector, IO_APIC_IRQ_MASK);
    return ZX_OK;
  }

  zx_status_t UnmaskInterrupt(unsigned int vector) {
    Guard<SpinLock, IrqSave> guard{&lock_};
    IoApic::MaskIrq(vector, IO_APIC_IRQ_UNMASK);
    return ZX_OK;
  }

  zx_status_t ConfigureInterrupt(unsigned int vector, enum interrupt_trigger_mode tm,
                                 enum interrupt_polarity pol) {
    Guard<SpinLock, IrqSave> guard{&lock_};
    IoApic::ConfigureIrq(vector, tm, pol, DELIVERY_MODE_FIXED, IO_APIC_IRQ_MASK, DST_MODE_PHYSICAL,
                         apic_bsp_id(), 0);
    return ZX_OK;
  }

  zx_status_t GetInterruptConfig(unsigned int vector, enum interrupt_trigger_mode* tm,
                                 enum interrupt_polarity* pol) {
    Guard<SpinLock, IrqSave> guard{&lock_};
    return IoApic::FetchIrqConfig(vector, tm, pol);
  }

  void GetEntryByX86Vector(uint8_t x86_vector, int_handler* handler, void** arg) {
    handler_table_[x86_vector].GetHandler(handler, arg);
  }

  // Returns true if the handler was present.  Must be called with
  // interrupts disabled.
  bool InvokeX86Vector(uint8_t x86_vector) { return handler_table_[x86_vector].InvokeIfPresent(); }

  // Register a handler for an external interrupt.
  // |vector| is a "global IRQ" number used by the IOAPIC module.
  //
  // If |handler| is nullptr, |arg| is ignored and the specified |vector| has
  // its current handler removed.
  //
  // If |handler| is not nullptr and no handler is currently installed for
  // |vector|, |handler| will be installed and will be invoked with argument
  // |arg| whenever that interrupt fires.
  //
  // If |handler| is not nullptr and a handler is already installed, this will
  // return ZX_ERR_ALREADY_BOUND.
  //
  // If no more CPU interrupt vectors are available, returns
  // ZX_ERR_NO_RESOURCES.
  zx_status_t RegisterInterruptHandler(unsigned int vector, int_handler handler, void* arg,
                                       bool permanent = false) {
    if (!IoApic::IsValidInterrupt(vector, 0 /* flags */)) {
      return ZX_ERR_INVALID_ARGS;
    }

    Guard<SpinLock, IrqSave> guard{&lock_};
    zx_status_t result = ZX_OK;

    /* Fetch the x86 vector currently configured for this global irq.  Force
     * its value to zero if it is currently invalid */
    uint8_t x86_vector = IoApic::FetchIrqVector(vector);
    if (x86_vector < X86_INT_PLATFORM_BASE || x86_vector > X86_INT_PLATFORM_MAX) {
      x86_vector = 0;
    }

    if (x86_vector == 0 && handler == nullptr) {
      return ZX_OK;
    }

    // If the vector already exists make sure it's not permanent and that we're allowed to modify it
    if (x86_vector && handler_table_[x86_vector].permanent()) {
      return ZX_ERR_ALREADY_BOUND;
    }

    if (x86_vector && !handler) {
      /* If the x86 vector is valid, and we are unregistering the handler,
       * return the x86 vector to the pool. */
      FreeHandler(x86_vector, 1);
    } else if (!x86_vector && handler) {
      /* If the x86 vector is invalid, and we are registering a handler,
       * attempt to get a new x86 vector from the pool. */
      uint range_start;

      /* Right now, there is not much we can do if the allocation fails.  In
       * debug builds, we ASSERT that everything went well.  In release
       * builds, we log a message and then silently ignore the request to
       * register a new handler. */

      result = AllocHandler(1, &range_start);

      if (result != ZX_OK) {
        TRACEF(
            "Failed to allocate x86 IRQ vector for global IRQ (%u) when "
            "registering new handler (%p, %p)\n",
            vector, handler, arg);
        return result;
      }

      DEBUG_ASSERT((range_start >= X86_INT_PLATFORM_BASE) && (range_start <= X86_INT_PLATFORM_MAX));
      x86_vector = (uint8_t)range_start;
    }

    DEBUG_ASSERT(x86_vector != 0);

    // Update the handler table and register the x86 vector with the io_apic.
    bool set = handler_table_[x86_vector].SetHandler(handler, arg, permanent);
    if (!set) {
      // If we're here, then RegisterInterruptHandler() was called on the
      // same vector twice to set the handler without clearing the handler
      // in-between.
      return ZX_ERR_ALREADY_BOUND;
    }

    IoApic::ConfigureIrqVector(vector, handler != nullptr ? x86_vector : 0);

    return ZX_OK;
  }

  zx_status_t MsiAllocBlock(uint requested_irqs, bool can_target_64bit, bool is_msix,
                            msi_block_t* out_block) {
    if (!out_block) {
      return ZX_ERR_INVALID_ARGS;
    }

    if (out_block->allocated) {
      return ZX_ERR_BAD_STATE;
    }

    if (!requested_irqs || (requested_irqs > MAX_MSI_IRQS)) {
      return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t res;
    uint alloc_start = 0;
    uint alloc_size = 1u << log2_uint_ceil(requested_irqs);

    {
      Guard<SpinLock, IrqSave> guard{&lock_};
      res = AllocHandler(alloc_size, &alloc_start);
    }
    if (res == ZX_OK) {
      // Compute the target address.
      // See section 10.11.1 of the Intel 64 and IA-32 Architectures Software
      // Developer's Manual Volume 3A.
      //
      // TODO(johngro) : don't just bind this block to the Local APIC of the
      // processor which is active when calling msi_alloc_block.  Instead,
      // there should either be a system policy (like, always send to any
      // processor, or just processor 0, or something), or the decision of
      // which CPUs to bind to should be left to the caller.
      uint32_t tgt_addr = 0xFEE00000;               // base addr
      tgt_addr |= ((uint32_t)apic_bsp_id()) << 12;  // Dest ID == the BSP APIC ID
      tgt_addr |= 0x08;                             // Redir hint == 1
      tgt_addr &= ~0x04;                            // Dest Mode == Physical

      // Compute the target data.
      // See section 10.11.2 of the Intel 64 and IA-32 Architectures Software
      // Developer's Manual Volume 3A.
      //
      // delivery mode == 0 (fixed)
      // trigger mode  == 0 (edge)
      // vector == start of block range
      DEBUG_ASSERT(!(alloc_start & ~0xFF));
      DEBUG_ASSERT(!(alloc_start & (alloc_size - 1)));
      uint32_t tgt_data = alloc_start;

      /* Success!  Fill out the bookkeeping and we are done */
      out_block->platform_ctx = NULL;
      out_block->base_irq_id = alloc_start;
      out_block->num_irq = alloc_size;
      out_block->tgt_addr = tgt_addr;
      out_block->tgt_data = tgt_data;
      out_block->allocated = true;
    }

    return res;
  }

  void MsiFreeBlock(msi_block_t* block) {
    DEBUG_ASSERT(block);
    DEBUG_ASSERT(block->allocated);
    {
      Guard<SpinLock, IrqSave> guard{&lock_};
      FreeHandler(block->base_irq_id, block->num_irq);
    }
    memset(block, 0, sizeof(*block));
  }

  void MsiRegisterHandler(const msi_block_t* block, uint msi_id, int_handler handler, void* ctx) {
    DEBUG_ASSERT(block && block->allocated);
    DEBUG_ASSERT(msi_id < block->num_irq);

    uint x86_vector = msi_id + block->base_irq_id;
    DEBUG_ASSERT((x86_vector >= X86_INT_PLATFORM_BASE) && (x86_vector <= X86_INT_PLATFORM_MAX));

    handler_table_[x86_vector].OverwriteHandler(handler, ctx);
  }

 private:
  // Representation of a single entry in the interrupt table, including a
  // lock to ensure a consistent view of the entry.
  class InterruptTableEntry {
   public:
    void GetHandler(int_handler* handler, void** arg) {
      Guard<SpinLock, IrqSave> guard{&lock_};
      *handler = handler_;
      *arg = arg_;
    }

    bool permanent() const {
      // Permanent handlers do not get modified once set, and are only set on startup, so we can use
      // relaxed loads.
      return permanent_.load(ktl::memory_order_relaxed);
    }

    // Returns true if the handler was present.  Must be called with
    // interrupts disabled.
    bool InvokeIfPresent() {
      if (permanent()) {
        // Once permanent is set to true we know that handler and arg are immutable and so it is
        // safe to read them without holding the lock.
        [this]() TA_NO_THREAD_SAFETY_ANALYSIS {
          DEBUG_ASSERT(handler_);
          handler_(arg_);
        }();
        return true;
      } else {
        Guard<SpinLock, NoIrqSave> guard{&lock_};
        if (handler_) {
          handler_(arg_);
          return true;
        }
        return false;
      }
    }

    // Set the handler for this entry.  If |handler| is nullptr, |arg| is
    // ignored.  Makes no change and returns false if |handler| is not
    // nullptr and this entry already has a handler assigned.
    bool SetHandler(int_handler handler, void* arg, bool make_permanent) {
      Guard<SpinLock, IrqSave> guard{&lock_};
      // Cannot modify existing permanent handlers.
      if (permanent()) {
        return false;
      }
      if (handler && handler_) {
        return false;
      }

      handler_ = handler;
      arg_ = handler ? arg : nullptr;
      permanent_.store(make_permanent, ktl::memory_order_relaxed);
      return true;
    }

    // Set the handler for this entry.  If |handler| is nullptr, |arg| is
    // ignored.
    void OverwriteHandler(int_handler handler, void* arg) {
      Guard<SpinLock, IrqSave> guard{&lock_};
      DEBUG_ASSERT(!permanent());
      handler_ = handler;
      arg_ = handler ? arg : nullptr;
    }

   private:
    mutable DECLARE_SPINLOCK(InterruptTableEntry) lock_;

    int_handler handler_ TA_GUARDED(lock_) = nullptr;
    void* arg_ TA_GUARDED(lock_) = nullptr;
    ktl::atomic<bool> permanent_ = false;
  };

  void FreeHandler(uint base, uint count) TA_REQ(lock_) {
    handler_allocated_.Clear(base, base + count);
  }

  // Allocates a range of handlers that are also aligned by the count, which must be a power of 2.
  zx_status_t AllocHandler(uint count, uint* start) TA_REQ(lock_) {
    DEBUG_ASSERT(fbl::is_pow2(count));
    // This is the anchor of our search. We always start at the beginning.
    size_t bitoff = X86_INT_PLATFORM_BASE;

    zx_status_t result;
    do {
      // Round the start of our search up to count (which is also our alignment).
      // Find will return an error if bitoff has exceeded the end of the range.
      bitoff = ROUNDUP(bitoff, count);
      result = handler_allocated_.Find(false, bitoff, X86_INT_PLATFORM_MAX + 1, count, &bitoff);
      // Bail early if we get any kind of error.
      if (result != ZX_OK) {
        return result;
      }
    } while ((bitoff % count) != 0);
    // Loop only exits if we found a valid range.
    *start = (uint)bitoff;
    return handler_allocated_.Set(bitoff, bitoff + count);
  }
  friend bool TestHandlerAllocationAlignment();

  // This lock guards against concurrent access to the IOAPIC and handler allocation bitmap.
  DECLARE_SPINLOCK(InterruptManager) lock_;

  // Handler table with one entry per CPU interrupt vector.
  InterruptTableEntry handler_table_[X86_INT_COUNT] = {};

  // Bitmap to track what entries in handler_table_ are in use.
  bitmap::RawBitmapGeneric<bitmap::FixedStorage<X86_INT_COUNT>> handler_allocated_
      TA_GUARDED(lock_);
};

#endif  // ZIRCON_KERNEL_PLATFORM_PC_INTERRUPT_MANAGER_H_
