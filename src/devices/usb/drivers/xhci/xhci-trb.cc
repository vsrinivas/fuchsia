// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>

#include <bits/limits.h>
#include <fbl/auto_lock.h>
#include <hw/arch_ops.h>

#include "xhci.h"

namespace usb_xhci {

zx_status_t xhci_transfer_ring_init(xhci_transfer_ring_t* ring, zx_handle_t bti_handle, int count) {
  fbl::AllocChecker ac;
  ddk::IoBuffer buffer;
  zx_status_t status =
      buffer.Init(bti_handle, count * sizeof(xhci_trb_t), IO_BUFFER_RW | XHCI_IO_BUFFER_UNCACHED);
  if (status != ZX_OK)
    return status;
  status = buffer.PhysMap();
  if (status != ZX_OK)
    return status;
  std::unique_ptr<IoBufferContainer> container(new (&ac) IoBufferContainer(std::move(buffer)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  size_t phys_count = (*container)->phys_count();
  const zx_paddr_t* sg_list = (*container)->phys_list();
  ring->start = static_cast<xhci_trb_t*>((*container)->virt());
  ring->current_trb = ring->start;
  ring->dequeue_ptr = ring->start;
  ring->full = false;
  ring->size = count - phys_count;  // subtract 1 for LINK TRB at the end
  ring->pcs = TRB_C;
  // set link TRB at end to point back to the beginning
  ring->start[count - 1].ptr = sg_list[0];
  trb_set_control(&ring->start[count - 1], TRB_LINK, TRB_TC);
  for (size_t i = 0; i < phys_count; i++) {
    if (i + 1 < phys_count) {
      xhci_trb_t* trb =
          reinterpret_cast<xhci_trb_t*>(reinterpret_cast<size_t>(ring->start) + (i * PAGE_SIZE) +
                                        (PAGE_SIZE - sizeof(xhci_trb_t)));
      XHCI_WRITE64(&trb->ptr, sg_list[i + 1]);
      XHCI_WRITE32(&trb->control, TRB_LINK << TRB_TYPE_START);
    }
    VirtualAddress address_mapping(reinterpret_cast<size_t>((*container)->virt()) +
                                   (PAGE_SIZE * i));
    address_mapping.phys_start = sg_list[i];
    ring->virt_to_phys_map[address_mapping] = address_mapping.phys_start;
    ring->phys_to_virt_map[address_mapping.phys_start / PAGE_SIZE] = address_mapping.virt_start;
  }
  ring->buffers.push_back(std::move(container));
  return ZX_OK;
}

void xhci_transfer_ring_free(xhci_transfer_ring_t* ring) {
  ring->buffers.clear();
  ring->virt_to_phys_map.clear();
  ring->phys_to_virt_map.clear();
}

// return the number of free TRBs in the ring
size_t xhci_transfer_ring_free_trbs(xhci_transfer_ring_t* ring) {
  xhci_trb_t* current = ring->current_trb;
  xhci_trb_t* dequeue_ptr = ring->dequeue_ptr;

  if (ring->full) {
    assert(current == dequeue_ptr);
    return 0;
  }

  auto size = ring->size;

  if (current < dequeue_ptr) {
    current += size;
  }

  size_t busy_count = current - dequeue_ptr;
  return size - busy_count;
}

zx_status_t xhci_event_ring_init(xhci_event_ring_t* ring, zx_handle_t bti_handle,
                                 erst_entry_t* erst_array, int count) {
  // allocate a read-only buffer for TRBs
  fbl::AllocChecker ac;
  ddk::IoBuffer buffer;
  zx_status_t status =
      buffer.Init(bti_handle, count * sizeof(xhci_trb_t), IO_BUFFER_RW | XHCI_IO_BUFFER_UNCACHED);
  if (status != ZX_OK)
    return status;
  status = buffer.PhysMap();
  if (status != ZX_OK)
    return status;
  size_t phys_count = buffer.phys_count();
  const zx_paddr_t* sg_list = buffer.phys_list();
  std::unique_ptr<IoBufferContainer> container(new (&ac) IoBufferContainer(std::move(buffer)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  ring->start = static_cast<xhci_trb_t*>((*container)->virt());
  ring->current = ring->start;
  ring->end = ring->start + count;
  ring->ccs = TRB_C;
  for (size_t i = 0; i < phys_count; i++) {
    VirtualAddress address_mapping(reinterpret_cast<size_t>((*container)->virt()) +
                                   (PAGE_SIZE * i));
    address_mapping.phys_start = sg_list[i];
    ring->virt_to_phys_map[address_mapping] = address_mapping.phys_start;
    ring->phys_to_virt_map[address_mapping.phys_start / PAGE_SIZE] = address_mapping.virt_start;
    XHCI_WRITE64(&erst_array[i].ptr, sg_list[i]);
    XHCI_WRITE32(&erst_array[i].size, PAGE_SIZE / sizeof(xhci_trb_t));
  }
  ring->buffers.push_back(std::move(container));
  return ZX_OK;
}

void xhci_event_ring_free(xhci_event_ring_t* ring) {
  ring->buffers.clear();
  ring->virt_to_phys_map.clear();
}

void xhci_clear_trb(xhci_trb_t* trb) {
  XHCI_WRITE64(&trb->ptr, 0);
  XHCI_WRITE32(&trb->status, 0);
  XHCI_WRITE32(&trb->control, 0);
}

void xhci_set_transfer_noop_trb(xhci_trb_t* trb) {
  uint32_t control = XHCI_READ32(&trb->control);
  if ((control & TRB_TYPE_MASK) == (TRB_LINK << TRB_TYPE_START)) {
    // Don't do anything if it's the LINK TRB.
    return;
  }
  XHCI_WRITE64(&trb->ptr, 0);
  XHCI_WRITE32(&trb->status, 0);
  // Preserve the cycle bit of the TRB.
  trb_set_control(trb, TRB_TRANSFER_NOOP, control & TRB_C);
}

xhci_trb_t* xhci_read_trb_ptr(xhci_transfer_ring_t* ring, xhci_trb_t* trb) {
  // convert physical address to virtual
  uintptr_t ptr = trb->ptr;
  return reinterpret_cast<xhci_trb_t*>(ring->phys_to_virt_map[ptr / PAGE_SIZE] + (ptr % PAGE_SIZE));
}
xhci_trb_t* xhci_next_evt(xhci_event_ring_t* ring, xhci_trb_t* trb) { return trb + 1; }
xhci_trb_t* xhci_get_next_trb(xhci_transfer_ring_t* ring, xhci_trb_t* trb) {
  trb++;
  uint32_t control = XHCI_READ32(&trb->control);
  if ((control & TRB_TYPE_MASK) == (TRB_LINK << TRB_TYPE_START)) {
    trb = xhci_read_trb_ptr(ring, trb);
  }
  return trb;
}

void xhci_increment_ring(xhci_transfer_ring_t* ring) {
  xhci_trb_t* trb = ring->current_trb;
  uint32_t control = XHCI_READ32(&trb->control);
  uint32_t chain = control & TRB_CHAIN;
  if (ring->pcs) {
    XHCI_WRITE32(&trb->control, control | ring->pcs);
  }
  trb = ++ring->current_trb;
  // check for LINK TRB
  control = XHCI_READ32(&trb->control);
  if ((control & TRB_TYPE_MASK) == (TRB_LINK << TRB_TYPE_START)) {
    control = (control & ~(TRB_CHAIN | TRB_C)) | chain | ring->pcs;
    XHCI_WRITE32(&trb->control, control);

    // toggle pcs if necessary
    if (control & TRB_TC) {
      ring->pcs ^= TRB_C;
    }
    ring->current_trb = xhci_read_trb_ptr(ring, trb);
  }

  if (ring->current_trb == ring->dequeue_ptr) {
    // We've just enqueued something, so if the pointers are equal,
    // the ring must be full.
    ring->full = true;
  }
}

void xhci_set_dequeue_ptr(xhci_transfer_ring_t* ring, xhci_trb_t* new_ptr) {
  ring->dequeue_ptr = new_ptr;
  ring->full = false;
}

xhci_trb_t* xhci_transfer_ring_phys_to_trb(xhci_transfer_ring_t* ring, zx_paddr_t phys) {
  return reinterpret_cast<xhci_trb_t*>(ring->phys_to_virt_map[phys / PAGE_SIZE] +
                                       (phys % PAGE_SIZE));
}

}  // namespace usb_xhci
