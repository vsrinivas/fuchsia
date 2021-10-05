// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_TRANSFER_RING_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_TRANSFER_RING_H_

#include <fuchsia/hardware/usb/request/c/banjo.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>

#include <map>

#include "xhci-context.h"
#include "xhci-event-ring.h"

namespace usb_xhci {

class UsbXhci;

struct ContiguousTRBInfo {
  cpp20::span<TRB> nop;   // Optional page of NOPs
  cpp20::span<TRB> trbs;  // Contiguous TRBs
  cpp20::span<TRB> first() {
    if (!nop.empty()) {
      return nop;
    }
    return trbs;
  }
};

// Used for queueing transfers to the XHCI controller
class TransferRing {
 public:
  struct State {
    TRB* trbs;
    // Producer Cycle state
    bool pcs;
  };
  TransferRing() : trb_context_allocator_(-1, true) {}
  bool IsIsochronous() { return isochronous_; }
  void SetIsochronous() { isochronous_ = true; }
  size_t GetShortCount() { return short_count_; }
  void ResetShortCount() { short_count_ = 0; }
  void IncrementShortCount(size_t size) { short_count_ += size; }
  zx_status_t AddTRB(const TRB& trb, std::unique_ptr<TRBContext> context);
  zx_status_t AssignContext(TRB* trb, std::unique_ptr<TRBContext> context, TRB* first_trb);
  // Handles a short packet. This will walk the TRB list up to and including short_trb
  // and return the number of bytes transferred. Returns ZX_ERR_IO if the transfer was
  // mis-aligned.
  zx_status_t HandleShortPacket(TRB* short_trb, size_t* transferred, TRB** first_trb,
                                size_t short_length);
  // Allocates a TRB but does not configure it.
  // It is the caller's responsibility to fully configure the returned TRB.
  // The caller may optionally rollback a transaction by calling Restore
  // The Cycle bit will be passed via the status field of the TRB.
  // The caller should store the Cycle bit locally and zero the status field
  // prior to doing anything else with the TRB.
  zx_status_t AllocateTRB(TRB** trb, State* state);

  // Allocates physically contiguous TRBs.
  // count is the number of TRBs to allocate (not the number of bytes)
  // NOP TRBs will be allocated with interrupt on complete set to 0 in order to pad the allocation
  // if not enough contiguous TRBs are available on the current page. If a contiguous allocation is
  // possible without the Transfer Ring spanning a page boundary, nullptr will be returned in the
  // nop field of the returned ContiguousTRBInfo. The pointer to the NOP TRBs will be returned in
  // the nop field of the returned ContiguousTRBInfo. The caller is responsible for setting the
  // Cycle bit to the correct value during the transaction commit stage. The pointer to the
  // contiguous TRB range will be returned in the trb field of the returned ContiguousTRBInfo.
  zx::status<ContiguousTRBInfo> AllocateContiguous(size_t count);
  State SaveState();
  void set_stall(bool stalled) {
    fbl::AutoLock l(&mutex_);
    stalled_ = stalled;
  }
  bool stalled() {
    fbl::AutoLock l(&mutex_);
    return stalled_;
  }
  State SaveStateLocked() __TA_REQUIRES(mutex_);
  void CommitLocked() __TA_REQUIRES(mutex_);
  // Commits the current page.
  void Commit();
  // Commits a multi-TRB transaction
  void CommitTransaction(const State& start);
  void Restore(const State& state);
  void RestoreLocked(const State& state) __TA_REQUIRES(mutex_);
  zx_status_t Init(size_t page_size, const zx::bti& bti, EventRing* ring, bool is_32bit,
                   ddk::MmioBuffer* mmio, const UsbXhci& hci);
  // Assumption: This function must ONLY be called from the interrupt
  // thread. Otherwise thread-safety assumptions are violated.
  zx_status_t DeinitIfActive() __TA_NO_THREAD_SAFETY_ANALYSIS;
  zx_status_t Deinit();
  bool active() {
    fbl::AutoLock l(&mutex_);
    return trbs_;
  }
  // Only called during initialization when no other threads are running.
  // Would be pointless to hold the mutex here.
  CRCR phys(uint8_t cap_length);
  // Retrieves command ring control register value of the next TRB that would be returned
  // by AllocateTRB.
  zx::status<CRCR> PeekCommandRingControlRegister(uint8_t cap_length);
  zx_paddr_t VirtToPhys(TRB* trb);
  zx_paddr_t VirtToPhysLocked(TRB* trb) __TA_REQUIRES(mutex_);
  TRB* PhysToVirt(zx_paddr_t paddr);
  TRB* PhysToVirtLocked(zx_paddr_t paddr) __TA_REQUIRES(mutex_);
  zx_status_t CompleteTRB(TRB* trb, std::unique_ptr<TRBContext>* context);
  fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> TakePendingTRBs();
  fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> TakePendingTRBsUntil(TRB* end);

  std::unique_ptr<TRBContext> AllocateContext() {
    fbl::AutoLock _(&mutex_);
    auto ctx = trb_context_allocator_.New();
    ctx->token = token_;
    return ctx;
  }

  TRBPromise AddressDeviceCommand(uint8_t slot_id, uint8_t port_id, std::optional<HubInfo> hub_info,
                                  bool bsr);

 private:
  bool AvailableSlots(size_t count) __TA_REQUIRES(mutex_);
  zx_status_t AllocInternal(Control control) __TA_REQUIRES(mutex_);
  fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> pending_trbs_ __TA_GUARDED(mutex_);
  // Allocates and initializes a buffer (transfer ring segment)
  // and increases the size of the ERST if necessary.
  // Each buffer can fit 1 page of TRBs
  // The dma_buffer::Buffer is owned by this TransferRing.
  zx_status_t AllocBuffer(dma_buffer::ContiguousBuffer** out) __TA_REQUIRES(mutex_);
  // Advances the enqueue pointer after a new element has been added to the transfer ring
  void AdvancePointer() __TA_REQUIRES(mutex_);
  fbl::Mutex mutex_;
  uint64_t token_ = 0;
  AllocatorType trb_context_allocator_ __TA_GUARDED(mutex_);
  fbl::DoublyLinkedList<std::unique_ptr<dma_buffer::ContiguousBuffer>> buffers_
      __TA_GUARDED(mutex_);
  std::map<zx_vaddr_t, dma_buffer::ContiguousBuffer*> virt_to_buffer_ __TA_GUARDED(mutex_);
  std::map<zx_paddr_t, dma_buffer::ContiguousBuffer*> phys_to_buffer_ __TA_GUARDED(mutex_);
  // Start of TRBs from perspective of enqueue pointer.
  // This pointer is incremented along with the enqueue pointer.
  TRB* trbs_ __TA_GUARDED(mutex_) = nullptr;
  zx_paddr_t trb_start_phys_ = 0;
  // Producer cycle bit (section 4.9.2)
  bool pcs_ __TA_GUARDED(mutex_) = true;
  TRB* dequeue_trb_ __TA_GUARDED(mutex_) = nullptr;
  // Capacity (number of TRBs, including link TRBs in ring)
  size_t capacity_ __TA_GUARDED(mutex_) = 0;
  size_t page_size_ __TA_GUARDED(mutex_);
  const zx::bti* bti_ __TA_GUARDED(mutex_);
  EventRing* ring_ __TA_GUARDED(mutex_);
  bool is_32_bit_ __TA_GUARDED(mutex_);
  ddk::MmioBuffer* mmio_ __TA_GUARDED(mutex_);
  // Whether or not this transfer ring is stalled.
  // When a transfer ring is stalled, TRBs added to it
  // will not be processed by the controller until a ResetEndpoint
  // command TRB is placed on the command ring and the command ring doorbell is rang.
  bool stalled_ __TA_GUARDED(mutex_);
  // Not guarded by a mutex since this is synchronized by the event ring.
  size_t short_count_ = 0;
  bool isochronous_ = false;
  const UsbXhci* hci_;
};

// The singleton xHCI command ring
class CommandRing : public TransferRing {
 public:
  zx_status_t Init(size_t page_size, zx::bti* bti, EventRing* ring, bool is_32bit,
                   ddk::MmioBuffer* mmio, UsbXhci* hci) {
    return TransferRing::Init(page_size, *bti, ring, is_32bit, mmio, *hci);
  }
  // Generates a NOP command
  TRB Nop() {
    TRB retval;
    Control::Get().FromValue(0).set_Type(Control::NopCommand).ToTrb(&retval);
    return retval;
  }
};
}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_TRANSFER_RING_H_
