// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cassert>

#include <bits/limits.h>
#include "trb-sizes.h"
#include "xhci-hw.h"
#include <ddk/io-buffer.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <memory>
#include <zircon/listnode.h>

namespace usb_xhci {

class IoBufferContainer;
// IO Buffer Container to allow nesting an IoBuffer
// in an intrusive doubly-linked list.
class IoBufferContainer : public fbl::DoublyLinkedListable<std::unique_ptr<IoBufferContainer>> {
 public:
  IoBufferContainer(ddk::IoBuffer buffer) : buffer_(std::move(buffer)) {}
  const ddk::IoBuffer* operator->() const { return &buffer_; }
  const ddk::IoBuffer& operator*() { return buffer_; }

 private:
  ddk::IoBuffer buffer_;
};

// Represents a virtual memory address mapping.
// Contains information about the virtual range,
// and physical starting address for contiguous mappings.
struct VirtualAddress {
  VirtualAddress() {}
  VirtualAddress(zx_vaddr_t virt_start, size_t virt_end)
      : virt_start(virt_start), virt_end(virt_end) {}
  VirtualAddress(size_t virt_start)
      : virt_start(virt_start), virt_end((virt_start + PAGE_SIZE) - 1) {}
  bool operator<(const VirtualAddress& other) const {
    return (virt_start / PAGE_SIZE) < (other.virt_start / PAGE_SIZE);
  }
  bool operator==(const VirtualAddress& other) const {
    return (virt_start / PAGE_SIZE) == (other.virt_start / PAGE_SIZE);
  }
  size_t GetKey() const { return virt_start / PAGE_SIZE; }
  zx_vaddr_t virt_start = 0;
  zx_vaddr_t virt_end = 0;
  size_t phys_start = 0;
};

// Constant-size map implementation with O(n) lookup time,
// and O(1) insertion time. Maintains a mapping of Keys to Values.
template <typename Key, typename Value, size_t Count>
class XhciMap {
 public:
  XhciMap() {}
  // Retrieves a given key, or creates one if it doesn't already exist
  // Asserts if the number of keys exceeds the statically-allocated buffer
  // size.
  Value& operator[](const Key& key) {
    for (size_t i = 0; i < len_; i++) {
      if (data_[i].first == key) {
        return data_[i].second;
      }
    }
    assert(len_ < Count);
    data_[len_].first = key;
    return data_[len_++].second;
  }
  // Retrieves the std::pair<Key, Value> for a given Key.
  // Returns nullptr if the key is not found in the map.
  std::pair<Key, Value>* get(const Key& key) {
    for (size_t i = 0; i < len_; i++) {
      if (data_[i].first == key) {
        return data_ + i;
      }
    }
    return nullptr;
  }
  // Removes all entries from this map
  void clear() {
    for (size_t i = 0; i < len_; i++) {
      data_[i] = {};
    }
    len_ = 0;
  }

 private:
  size_t len_ = 0;
  std::pair<Key, Value> data_[Count];
};

// used for both command ring and transfer rings
struct xhci_transfer_ring_t {
  fbl::DoublyLinkedList<std::unique_ptr<IoBufferContainer>> buffers;
  XhciMap<VirtualAddress, uint64_t, TRANSFER_RING_SIZE / sizeof(xhci_trb_t)> virt_to_phys_map;
  // Map of physical page indices to virtual addresses
  XhciMap<zx_paddr_t, zx_vaddr_t, TRANSFER_RING_SIZE / sizeof(xhci_trb_t)> phys_to_virt_map;
  xhci_trb_t* start;
  xhci_trb_t* current_trb;  // next to be filled by producer
  uint8_t pcs;              // producer cycle status
  xhci_trb_t* dequeue_ptr;  // next to be processed by consumer
                            // (not used for command ring)
  size_t size;              // number of TRBs in ring
  bool full;                // true if there are no available TRBs,
                            // this is needed to differentiate between
                            // an empty and full ring state
  fbl::Mutex xfer_lock;
};

struct EventMapping {
  uint64_t phys = 0;
  xhci_trb_t* next = nullptr;
  EventMapping() {}
  EventMapping(uint64_t phys) : phys(phys) {}
};

struct xhci_event_ring_t {
  fbl::DoublyLinkedList<std::unique_ptr<IoBufferContainer>> buffers;
  XhciMap<VirtualAddress, EventMapping, TRANSFER_RING_SIZE / sizeof(xhci_trb_t)> virt_to_phys_map;
  XhciMap<zx_paddr_t, zx_vaddr_t, TRANSFER_RING_SIZE / sizeof(xhci_trb_t)> phys_to_virt_map;
  xhci_trb_t* start;
  xhci_trb_t* current;
  xhci_trb_t* end;
  uint8_t ccs;  // consumer cycle status
  fbl::Mutex xfer_lock;
};

zx_status_t xhci_transfer_ring_init(xhci_transfer_ring_t* tr, zx_handle_t bti_handle, int count);
void xhci_transfer_ring_free(xhci_transfer_ring_t* ring);
size_t xhci_transfer_ring_free_trbs(xhci_transfer_ring_t* ring);
zx_status_t xhci_event_ring_init(xhci_event_ring_t*, zx_handle_t bti_handle,
                                 erst_entry_t* erst_array, int count);
void xhci_event_ring_free(xhci_event_ring_t* ring);
void xhci_clear_trb(xhci_trb_t* trb);
// Converts a transfer trb into a NO-OP transfer TRB, does nothing if it is the LINK TRB.
void xhci_set_transfer_noop_trb(xhci_trb_t* trb);
xhci_trb_t* xhci_read_trb_ptr(xhci_transfer_ring_t* ring, xhci_trb_t* trb);
xhci_trb_t* xhci_next_evt(xhci_event_ring_t* ring, xhci_trb_t* trb);
xhci_trb_t* xhci_get_next_trb(xhci_transfer_ring_t* ring, xhci_trb_t* trb);
void xhci_increment_ring(xhci_transfer_ring_t* ring);
void xhci_set_dequeue_ptr(xhci_transfer_ring_t* ring, xhci_trb_t* new_ptr);

// Returns the TRB corresponding to the given physical address, or nullptr if the address is
// invalid.
xhci_trb_t* xhci_transfer_ring_phys_to_trb(xhci_transfer_ring_t* ring, zx_paddr_t phys);

static inline zx_paddr_t xhci_transfer_ring_current_phys(xhci_transfer_ring_t* ring) {
  auto physmap =
      ring->virt_to_phys_map.get(VirtualAddress(reinterpret_cast<size_t>(ring->current_trb)));
  if (physmap == nullptr) {
    abort();
  }
  return physmap->second +
         (reinterpret_cast<size_t>(ring->current_trb) - physmap->first.virt_start);
}

static inline zx_paddr_t xhci_event_ring_current_phys(xhci_event_ring_t* ring) {
  auto physmap =
      ring->virt_to_phys_map.get(VirtualAddress(reinterpret_cast<size_t>(ring->current)));
  if (physmap == nullptr) {
    abort();
  }
  return physmap->second.phys +
         (reinterpret_cast<size_t>(ring->current) - physmap->first.virt_start);
}
struct xhci_t;
// Enlarges the XHCI rings. The caller must ensure exclusive ownership of the rings
// before invoking this function. Refer to XHCI 4.9.2.3
zx_status_t xhci_enlarge_ring(xhci_t* xhci, xhci_transfer_ring_t* transfer);

}  // namespace usb_xhci
