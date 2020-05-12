// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xhci-transfer-ring.h"

#include "usb-xhci.h"

namespace usb_xhci {
void TransferRing::AdvancePointer() {
  if ((reinterpret_cast<size_t>(trbs_) / PAGE_SIZE) !=
      (reinterpret_cast<size_t>(trbs_ + 1) / PAGE_SIZE)) {
    CommitLocked();
    trbs_ = static_cast<TRB*>(
        (*virt_to_buffer_.find((reinterpret_cast<size_t>(trbs_) / PAGE_SIZE) + 1)->second).virt());
  } else {
    trbs_++;
  }
  Control control = Control::FromTRB(trbs_);
  if (control.Type() == Control::Link) {
    zx_paddr_t ptr = trbs_->ptr;
    control.set_Cycle(pcs_).ToTrb(trbs_);
    // Read link pointer
    if (control.EntTC()) {
      pcs_ = !pcs_;
    }
    CommitLocked();
    zx_vaddr_t base_virt =
        reinterpret_cast<zx_vaddr_t>((*(phys_to_buffer_.find(ptr / PAGE_SIZE)->second)).virt());
    trbs_ = reinterpret_cast<TRB*>(base_virt + (ptr % PAGE_SIZE));
  }
}

zx_status_t TransferRing::AllocInternal(Control control) {
  // Reserve two additional TRBs for expanding the ring
  if (!AvailableSlots(2)) {
    // Attempt to grow the ring
    dma_buffer::ContiguousBuffer* new_trb;
    zx_status_t status = AllocBuffer(&new_trb);
    if (status != ZX_OK) {
      return status;
    }
    auto link_state = SaveStateLocked();
    TRB* link_trb = trbs_;
    control.set_Type(Control::Nop).ToTrb(trbs_);
    // Advance to the next spare TRB (this will go AFTER the link TRB)
    // Update size of ring
    // NOTE: This might bring us to a link TRB.
    // We shouldn't overwrite an existing link TRB.
    // It should be swapped with the new buffer instead
    capacity_ += (*new_trb).size() / sizeof(TRB);
    trbs_++;
    TRB* spare_trb = trbs_;
    Control spare_control = Control::FromTRB(spare_trb);
    if (spare_control.Type() == Control::Link) {
      // Special case for link TRBs. Simply swap links and update.
      reinterpret_cast<TRB*>((reinterpret_cast<size_t>((*new_trb).virt()) + (*new_trb).size()) -
                             sizeof(TRB))
          ->ptr = spare_trb->ptr;
      spare_trb->ptr = (*new_trb).phys();
      hw_mb();
      if (spare_control.EntTC()) {
        // Special case -- append new segment to last TRB (requires PCS toggle)
        pcs_ = !pcs_;
      }
      Control::Get()
          .FromValue(0)
          .set_Type(Control::Link)
          .set_EntTC(spare_control.EntTC())
          .set_Cycle(!pcs_)
          .ToTrb(reinterpret_cast<TRB*>(
              (reinterpret_cast<size_t>((*new_trb).virt()) + (*new_trb).size()) - sizeof(TRB)));
      spare_control.set_EntTC(0).ToTrb(spare_trb);
      RestoreLocked(link_state);
      return ZX_OK;
    }
    // Update pointer from new buffer to point to the spare TRB
    ZX_ASSERT((*new_trb).size() == PAGE_SIZE);
    reinterpret_cast<TRB*>((reinterpret_cast<size_t>((*new_trb).virt()) + (*new_trb).size()) -
                           sizeof(TRB))
        ->ptr = VirtToPhysLocked(spare_trb);
    Control::Get()
        .FromValue(0)
        .set_Type(Control::Link)
        .set_EntTC(0)
        .set_Cycle(!pcs_)
        .ToTrb(reinterpret_cast<TRB*>(
            (reinterpret_cast<size_t>((*new_trb).virt()) + (*new_trb).size()) - sizeof(TRB)));
    // Update pointer to new segment (adding the new segment to the ring)
    link_trb->ptr = (*new_trb).phys();
    link_trb->status = 0;
    // Restore the previous state
    RestoreLocked(link_state);
    hw_mb();
    Control::Get()
        .FromValue(0)
        .set_Type(Control::Link)
        .set_Cycle(pcs_)
        .set_EntTC(0)
        .ToTrb(link_trb);
    // Advance into the new segment
    // Advance two spots, one for the link TRB, and one for the new TRB
    // PCS stays the same across this transition.
    CommitLocked();
    trbs_ = static_cast<TRB*>((*new_trb).virt());
    ZX_ASSERT(Control::FromTRB(trbs_).Type() != Control::Link);
    ZX_ASSERT(trbs_ == (*new_trb).virt());
    return ZX_OK;
  }
  if (!AvailableSlots(1)) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

void TransferRing::Commit() {
  if (!hci_->HasCoherentState()) {
    fbl::AutoLock l(&mutex_);
    CommitLocked();
  }
}

void TransferRing::CommitLocked() {
  if (!hci_->HasCoherentState()) {
    InvalidatePageCache(trbs_, ZX_CACHE_FLUSH_DATA);
  }
}

void TransferRing::CommitTransaction(const State& start) {
  if (!hci_->HasCoherentState()) {
    fbl::AutoLock l(&mutex_);
    size_t current_page = reinterpret_cast<size_t>(start.trbs);
    current_page = fbl::round_down(current_page, static_cast<size_t>(PAGE_SIZE));
    bool ccs = start.pcs;
    TRB* current = start.trbs;
    while (Control::FromTRB(current).Cycle() == ccs) {
      auto control = Control::FromTRB(current);
      if (control.Type() == Control::Link) {
        if (control.EntTC()) {
          ccs = !ccs;
        }
        InvalidatePageCache(reinterpret_cast<void*>(current_page), ZX_CACHE_FLUSH_DATA);
        current = PhysToVirtLocked(current->ptr);
        current_page = reinterpret_cast<size_t>(current);
        current_page = fbl::round_down(current_page, static_cast<size_t>(PAGE_SIZE));
      } else {
        current++;
      }
    }
    InvalidatePageCache(current, ZX_CACHE_FLUSH_DATA);
  }
}

zx_status_t TransferRing::AddTRB(const TRB& trb, std::unique_ptr<TRBContext> context) {
  fbl::AutoLock l(&mutex_);
  if (context->token != token_) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = ring_->AddTRB();
  Control control = Control::FromTRB(trbs_);
  status = AllocInternal(control);
  if (status != ZX_OK) {
    return status;
  }
  if (Control::FromTRB(trbs_).Type() == Control::Link) {
    return ZX_ERR_BAD_STATE;
  }
  context->trb = trbs_;
  control = Control::Get().FromValue(trb.control);
  control.set_Cycle(pcs_);
  trbs_->ptr = trb.ptr;
  trbs_->status = 0;
  // Control must be the last thing to be written -- to ensure that ptr points to a valid location
  // in memory
  hw_mb();
  control.ToTrb(trbs_);
  hw_mb();
  AdvancePointer();
  pending_trbs_.push_back(std::move(context));
  if (!hci_->HasCoherentState()) {
    InvalidatePageCache(trbs_, ZX_CACHE_FLUSH_DATA);
  }
  return ZX_OK;
}

zx_status_t TransferRing::AssignContext(TRB* trb, std::unique_ptr<TRBContext> context) {
  fbl::AutoLock l(&mutex_);
  if (context->token != token_) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = AllocInternal(Control::FromTRB(trbs_));
  if (status != ZX_OK) {
    return status;
  }
  context->first_trb = first_trb;
  context->trb = trb;
  pending_trbs_.push_back(std::move(context));
  return ZX_OK;
}

TransferRing::State TransferRing::SaveState() {
  fbl::AutoLock l(&mutex_);
  return SaveStateLocked();
}
TransferRing::State TransferRing::SaveStateLocked() {
  State state;
  state.pcs = pcs_;
  state.trbs = trbs_;
  return state;
}
void TransferRing::Restore(const State& state) {
  fbl::AutoLock l(&mutex_);
  RestoreLocked(state);
}
void TransferRing::RestoreLocked(const State& state) {
  trbs_ = state.trbs;
  pcs_ = state.pcs;
}

zx_status_t TransferRing::Init(size_t page_size, const zx::bti& bti, EventRing* ring, bool is_32bit,
                               ddk::MmioBuffer* mmio, const UsbXhci& hci) {
  fbl::AutoLock l(&mutex_);
  if (trbs_ != nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  page_size_ = page_size;
  bti_ = &bti;
  ring_ = ring;
  is_32_bit_ = is_32bit;
  mmio_ = mmio;
  dma_buffer::ContiguousBuffer* container;
  isochronous_ = false;
  token_++;
  hci_ = &hci;
  return AllocBuffer(&container);
}

zx_status_t TransferRing::DeinitIfActive() {
  if (trbs_) {
    return Deinit();
  }
  return ZX_OK;
}
zx_status_t TransferRing::Deinit() {
  fbl::AutoLock l(&mutex_);
  if (!trbs_) {
    return ZX_ERR_BAD_STATE;
  }
  trbs_ = nullptr;
  dequeue_trb_ = nullptr;
  pcs_ = true;
  buffers_.clear();
  virt_to_buffer_.clear();
  isochronous_ = false;
  phys_to_buffer_.clear();
  ring_->RemovePressure();
  return ZX_OK;
}

CRCR TransferRing::TransferRing::phys(uint8_t cap_length) {
  fbl::AutoLock l(&mutex_);
  CRCR cr = CRCR::Get(cap_length).FromValue(trb_start_phys_);
  ZX_ASSERT(trb_start_phys_);
  cr.set_RCS(pcs_);
  return cr;
}
zx_paddr_t TransferRing::VirtToPhys(TRB* trb) {
  fbl::AutoLock l(&mutex_);
  return VirtToPhysLocked(trb);
}
zx_paddr_t TransferRing::VirtToPhysLocked(TRB* trb) {
  const auto& buffer = virt_to_buffer_.find(reinterpret_cast<zx_vaddr_t>(trb) / PAGE_SIZE);
  auto offset = reinterpret_cast<zx_vaddr_t>(trb) % PAGE_SIZE;
  return buffer->second->phys() + offset;
}
TRB* TransferRing::PhysToVirt(zx_paddr_t paddr) {
  fbl::AutoLock l(&mutex_);
  return PhysToVirtLocked(paddr);
}
TRB* TransferRing::PhysToVirtLocked(zx_paddr_t paddr) {
  const auto& buffer = phys_to_buffer_.find(paddr / PAGE_SIZE);
  auto offset = paddr % PAGE_SIZE;
  auto vaddr = reinterpret_cast<zx_vaddr_t>(buffer->second->virt()) + offset;
  return reinterpret_cast<TRB*>(vaddr);
}
zx_status_t TransferRing::CompleteTRB(TRB* trb, std::unique_ptr<TRBContext>* context) {
  fbl::AutoLock l(&mutex_);
  if (pending_trbs_.is_empty()) {
    return ZX_ERR_CANCELED;
  }
  dequeue_trb_ = trb;
  *context = pending_trbs_.pop_front();
  if (trb != (*context)->trb) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}
fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> TransferRing::TakePendingTRBs() {
  fbl::AutoLock l(&mutex_);
  return std::move(pending_trbs_);
}
fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> TransferRing::TakePendingTRBsUntil(TRB* end) {
  fbl::AutoLock l(&mutex_);
  dequeue_trb_ = end;
  fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> retval;
  while (!pending_trbs_.is_empty()) {
    auto trb = pending_trbs_.pop_front();
    bool is_end = trb->trb == end;
    retval.push_back(std::move(trb));
    if (is_end) {
      break;
    }
  }
  return retval;
}

zx_status_t TransferRing::AllocateTRB(TRB** trb, State* state) {
  fbl::AutoLock l(&mutex_);
  if (state) {
    state->pcs = pcs_;
    state->trbs = trbs_;
  }
  ring_->AddTRB();
  Control control = Control::FromTRB(trbs_);
  zx_status_t status = AllocInternal(control);
  if (status != ZX_OK) {
    return status;
  }
  if (Control::FromTRB(trbs_).Type() == Control::Link) {
    return ZX_ERR_BAD_STATE;
  }
  trbs_->ptr = 0;
  trbs_->status = pcs_;
  *trb = trbs_;
  AdvancePointer();
  return ZX_OK;
}

zx_status_t TransferRing::AllocBuffer(dma_buffer::ContiguousBuffer** out) {
  std::unique_ptr<dma_buffer::ContiguousBuffer> buffer;
  {
    std::unique_ptr<dma_buffer::ContiguousBuffer> buffer_tmp;
    zx_status_t status = hci_->factory().CreateContiguous(
        *bti_, page_size_, static_cast<uint32_t>(page_size_ == PAGE_SIZE ? 0 : page_size_ >> 12),
        &buffer_tmp);
    if (status != ZX_OK) {
      return status;
    }
    buffer = std::move(buffer_tmp);
  }
  if (is_32_bit_ && (buffer->phys() >= UINT32_MAX)) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_vaddr_t virt = reinterpret_cast<zx_vaddr_t>(buffer->virt());
  zx_paddr_t phys = buffer->phys();
  const size_t count = page_size_ / sizeof(TRB);
  TRB* trbs = reinterpret_cast<TRB*>(virt);
  Control control = Control::Get().FromValue(0);
  control.set_Type(Control::Link);
  if (!trbs_) {
    trbs_ = trbs;
    capacity_ = count;
    trb_start_phys_ = phys;
    dequeue_trb_ = trbs_;
  }
  control.set_EntTC(1);
  trbs[count - 1].ptr = trb_start_phys_;
  hw_mb();
  control.ToTrb(trbs + (count - 1));
  zx_status_t status = ring_->AddSegmentIfNone();
  if (status != ZX_OK) {
    return status;
  }
  virt_to_buffer_[virt / PAGE_SIZE] = buffer.get();
  phys_to_buffer_[phys / PAGE_SIZE] = buffer.get();
  *out = buffer.get();
  buffers_.push_back(std::move(buffer));
  if (!pcs_) {
    for (size_t i = 0; i < count; i++) {
      Control::FromTRB(trbs + i).set_Cycle(1).ToTrb(trbs + i);
    }
  }
  zx_cache_flush(reinterpret_cast<void*>(virt), page_size_, ZX_CACHE_FLUSH_DATA);
  return ZX_OK;
}

bool TransferRing::AvailableSlots(size_t count) {
  TRB* current = trbs_ + 1;
  while (count) {
    if (current == dequeue_trb_) {
      return false;
    }
    Control control = Control::FromTRB(current);
    if (control.Type() == Control::Link) {
      current = PhysToVirtLocked(current->ptr);
      // Don't count link TRBs as available slots. We can't actually
      // put data in them.
      continue;
    } else {
      current++;
    }
    count--;
  }
  return true;
}

}  // namespace usb_xhci
