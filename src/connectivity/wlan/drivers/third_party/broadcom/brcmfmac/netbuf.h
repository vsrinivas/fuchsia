// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_NETBUF_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_NETBUF_H_

// This file transitionally contains two "netbuf" implementations:
// * Netbuf, which is a RAII class for ferrying external buffer instances around the brcmfmac
//   driver.
// * brcmf_netbuf, which is a transitional C struct used the last vestiges of the port from the
//   Linux driver.  This is analogous to sk_buff.

// Includes for Netbuf.

#include <zircon/types.h>

#include <limits>
#include <memory>

#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>

// Additional includes for brcmf_netbuf.

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define _ALL_SOURCE
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/listnode.h>

#define SDIOD_SIZE_ALIGNMENT 4

namespace wlan {
namespace brcmfmac {

// The Netbuf class holds a sized memory buffer, which may have been allocated elsewhere, and
// ensures that any buffer return callbacks are held and called appropriately when the buffer's
// utility is complete.
class Netbuf {
 public:
  // Construct an empty Netbuf instance.
  Netbuf();

  // Destroy a Netbuf instance.  If Return() has not already been called, it is called with
  // ZX_ERR_INTERNAL.
  virtual ~Netbuf();

  // State accessors.
  const void* data() const;
  size_t size() const;
  int priority() const;

  // State setters
  void SetPriority(int priority);

  // Return ownership of the underlying buffer, calling a completion callback with the given status
  // as a parameter.  After this call, the Netbuf is in the empty state; if Return() is called
  // again, it causes a debug assertion and does nothing.
  virtual void Return(zx_status_t status);

 protected:
  const void* data_ = nullptr;
  size_t size_ = 0;
  int priority_ = 0;
};

// This class implements Netbuf for use with an ethernet_netbuf_t instance obtained through the DDK
// interface.
class EthernetNetbuf : public Netbuf {
 public:
  EthernetNetbuf();
  // Construct an EthernetNetbuf instance from an ethernet_netbuf_t instance, completion callback,
  // and cookie.  The completion callback will be invoked when the instance is destroyed.
  explicit EthernetNetbuf(ethernet_netbuf_t* netbuf, ethernet_impl_queue_tx_callback completion_cb,
                          void* cookie);
  EthernetNetbuf(const EthernetNetbuf& other) = delete;
  EthernetNetbuf(EthernetNetbuf&& other);
  EthernetNetbuf& operator=(EthernetNetbuf other);
  friend void swap(EthernetNetbuf& lhs, EthernetNetbuf& rhs);
  ~EthernetNetbuf() override;

  void Return(zx_status_t status) override;

 private:
  ethernet_netbuf_t* netbuf_ = nullptr;
  ethernet_impl_queue_tx_callback completion_cb_ = nullptr;
  void* cookie_ = nullptr;
};

// This class implements Netbuf using an owned, allocated array.
class AllocatedNetbuf : public Netbuf {
 public:
  AllocatedNetbuf();
  // Construct an AllocatedNetbuf instance.  ITs storage will be deallocated when the instance is
  // destroyed.
  explicit AllocatedNetbuf(std::unique_ptr<char[]> allocation, size_t size);
  AllocatedNetbuf(const AllocatedNetbuf& other) = delete;
  AllocatedNetbuf(AllocatedNetbuf&& other);
  AllocatedNetbuf& operator=(AllocatedNetbuf other);
  friend void swap(AllocatedNetbuf& lhs, AllocatedNetbuf& rhs);
  ~AllocatedNetbuf() override;

  void Return(zx_status_t status) override;

 private:
  std::unique_ptr<char[]> allocation_;
};

}  // namespace brcmfmac
}  // namespace wlan

//
// Transitional brcmf_netbuf definitions below.
//

// Purpose of this library:
//
// Store packet data (network or firmware-control packets) in buffers that can be queued, and
// whose head and tail can be adjusted (similar to Linux skbuff) while the buffer is being
// processed.

/*
 * Fields:
 *
 * priority - The priority of this packet
 * len - The length of data stored
 * data - Pointer to the start of data - may be moved by shrink_head/grow_head
 * listnode - Used to maintain queues of brcmf_netbuf
 * workspace - Reserved for use by the driver (a holdover from Linux-style architecture)
 * ip_summed - Unclear - TODO: maybe delete after FIDL interfaces is finalized
 * allocated_buffer - Pointer to the allocated buffer
 * allocated_size - Size of the allocated buffer
 */
struct brcmf_netbuf {
  int priority;
  uint32_t len;
  uint8_t* data;
  list_node_t listnode;  // Do not access listnode fields directly.
  // Workspace is a small area for use by the driver, on which a driver-specific struct can
  // be superimposed. For example, see fwsignal.c for brcmf_netbuf_workspace.
  // The driver uses it to associate state / information with the packet.
  // Code above and below the driver, and the netbuf library, should not modify this area.
  uint8_t workspace[48];
  uint32_t ip_summed;
  uint8_t* allocated_buffer;
  uint32_t allocated_size;
};

// The list-head for queues of buffers.
struct brcmf_netbuf_list;

// ==== Buffer alloc and manipulation functions ====

// Allocate a netbuf, with room to store the requested amount of data.
struct brcmf_netbuf* brcmf_netbuf_allocate(uint32_t size);

// Free the netbuf and any dependent buffers.
void brcmf_netbuf_free(struct brcmf_netbuf* netbuf);

// Return the amount of space available to grow/expand the head.
static inline uint32_t brcmf_netbuf_head_space(struct brcmf_netbuf* netbuf);

// Return the amount of space available to grow/expand the tail.
static inline uint32_t brcmf_netbuf_tail_space(struct brcmf_netbuf* netbuf);

// Increase the space officially available for use at the end of the buffer's data.
static inline void brcmf_netbuf_grow_tail(struct brcmf_netbuf* netbuf, uint32_t len);

// Chop bytes off the start of the buffer's data.
static inline void brcmf_netbuf_shrink_head(struct brcmf_netbuf* netbuf, uint32_t len);

// Expand space available to prepend data.
static inline void brcmf_netbuf_grow_head(struct brcmf_netbuf* netbuf, uint32_t len);

// Set length by adjusting the tail.
static inline void brcmf_netbuf_set_length_to(struct brcmf_netbuf* netbuf, uint32_t len);

// If buffer's data length is already smaller than requested, NOP; otherwise, shrink it.
static inline void brcmf_netbuf_reduce_length_to(struct brcmf_netbuf* netbuf, uint32_t len);

// Realloc the buffer, expanding head and/or tail by requested amount. Does not change data size.
static inline zx_status_t brcmf_netbuf_grow_realloc(struct brcmf_netbuf* netbuf, uint32_t head,
                                                    uint32_t tail);

// ==== Buffer list functions ====

// Initialize a lock-protected list.
static inline void brcmf_netbuf_list_init(struct brcmf_netbuf_list* list);

// Used for consistency checking prior to freeing. Since there's no way to know the list this
// netbuf used to be on, this function doesn't take the list lock; thus it may cause spurious
// warnings on some architectures if the field-setting hasn't propagated to the core this function
// is running on by the time this check is run. Caveat user.
static inline bool brcmf_netbuf_maybe_in_list(struct brcmf_netbuf* netbuf);

// Return the length of the list.
static inline uint32_t brcmf_netbuf_list_length(struct brcmf_netbuf_list* list);

// Return true iff the list is empty.
static inline bool brcmf_netbuf_list_is_empty(struct brcmf_netbuf_list* list);

// Add element to the head of the list.
static inline void brcmf_netbuf_list_add_head(struct brcmf_netbuf_list* list,
                                              struct brcmf_netbuf* element);

// Return the previous entry in the list, or NULL if element is the first entry.
static inline struct brcmf_netbuf* brcmf_netbuf_list_prev(struct brcmf_netbuf_list* list,
                                                          struct brcmf_netbuf* element);

// Return the next entry in the list, or NULL if element is the last entry.
static inline struct brcmf_netbuf* brcmf_netbuf_list_next(struct brcmf_netbuf_list* list,
                                                          struct brcmf_netbuf* element);

// Add element to the end of the list.
static inline void brcmf_netbuf_list_add_tail(struct brcmf_netbuf_list* list,
                                              struct brcmf_netbuf* element);

// Add new_element to list after target.
static inline void brcmf_netbuf_list_add_after(struct brcmf_netbuf_list* list,
                                               struct brcmf_netbuf* target,
                                               struct brcmf_netbuf* new_element);

// Unlink element from the list.
static inline void brcmf_netbuf_list_remove(struct brcmf_netbuf_list* list,
                                            struct brcmf_netbuf* element);

// Remove and return the first element of the list, or NULL if list is empty.
static inline struct brcmf_netbuf* brcmf_netbuf_list_remove_head(struct brcmf_netbuf_list* list);

// Remove and return the last element of the list, or NULL if list is empty.
static inline struct brcmf_netbuf* brcmf_netbuf_list_remove_tail(struct brcmf_netbuf_list* list);

// Return the first element of the list, or NULL if empty.
static inline struct brcmf_netbuf* brcmf_netbuf_list_peek_head(struct brcmf_netbuf_list* list);

// Return the last element of the list, or NULL if empty.
static inline struct brcmf_netbuf* brcmf_netbuf_list_peek_tail(struct brcmf_netbuf_list* list);

// Iterate over the list, assigning "entry" to each element in turn. NOT protected by the list lock.
// #define brcmf_netbuf_list_for_every(buf_list, entry)

// Iterate over the list, assigning "entry" to each element in turn, using "temp" to support
// element deletion. NOT protected by the list lock.
//#define brcmf_netbuf_list_for_every_safe(buf_list, entry, temp)

// Below this line is the implementation.
// Do not access qlen, listnode.prev, and listnode.next directly, since
// that is not threadsafe (they're not atomic, so all accesses must be inside the list lock).

struct brcmf_netbuf_list {
  uint32_t priority;
  mtx_t lock;
  uint32_t qlen;
  list_node_t listnode;
};

struct brcmf_netbuf* brcmf_netbuf_allocate(uint32_t size);

void brcmf_netbuf_free(struct brcmf_netbuf* netbuf);

static inline uint32_t brcmf_netbuf_head_space(struct brcmf_netbuf* netbuf) {
  const ptrdiff_t space = netbuf->data - netbuf->allocated_buffer;
  ZX_DEBUG_ASSERT(space <= std::numeric_limits<uint32_t>::max());
  return static_cast<uint32_t>(space);
}

static inline uint32_t brcmf_netbuf_tail_space(struct brcmf_netbuf* netbuf) {
  return netbuf->allocated_size - netbuf->len - brcmf_netbuf_head_space(netbuf);
}

static inline void brcmf_netbuf_grow_tail(struct brcmf_netbuf* netbuf, uint32_t len) {
  ZX_DEBUG_ASSERT(netbuf->data + netbuf->len + len <=
                  netbuf->allocated_buffer + netbuf->allocated_size);
  netbuf->len += len;
}

static inline void brcmf_netbuf_shrink_head(struct brcmf_netbuf* netbuf, uint32_t len) {
  ZX_DEBUG_ASSERT(netbuf->len >= len);
  netbuf->data += len;
  netbuf->len -= len;
}

static inline void brcmf_netbuf_grow_head(struct brcmf_netbuf* netbuf, uint32_t len) {
  ZX_DEBUG_ASSERT(brcmf_netbuf_head_space(netbuf) >= len);
  netbuf->data -= len;
  netbuf->len += len;
}

static inline void brcmf_netbuf_set_length_to(struct brcmf_netbuf* netbuf, uint32_t len) {
  ZX_DEBUG_ASSERT(netbuf->len + brcmf_netbuf_tail_space(netbuf) >= len);
  netbuf->len = len;
}

static inline void brcmf_netbuf_reduce_length_to(struct brcmf_netbuf* netbuf, uint32_t len) {
  if (len > netbuf->len) {
    return;
  }
  ZX_DEBUG_ASSERT(netbuf->len + brcmf_netbuf_tail_space(netbuf) >= len);
  netbuf->len = len;
}

static inline zx_status_t brcmf_netbuf_grow_realloc(struct brcmf_netbuf* netbuf, uint32_t head,
                                                    uint32_t tail) {
  uint32_t old_data_offset = brcmf_netbuf_head_space(netbuf);
  uint8_t* new_buffer =
      (uint8_t*)realloc(netbuf->allocated_buffer, netbuf->allocated_size + head + tail);
  if (new_buffer == NULL) {
    return ZX_ERR_NO_MEMORY;
  } else {
    netbuf->allocated_buffer = new_buffer;
  }
  if (head != 0) {
    // Safer to copy the whole allocated_buffer.
    memmove(netbuf->allocated_buffer + head, netbuf->allocated_buffer, netbuf->allocated_size);
  }
  netbuf->allocated_size += head + tail;
  netbuf->data = netbuf->allocated_buffer + old_data_offset + head;
  return ZX_OK;
}

static inline void brcmf_netbuf_list_init(struct brcmf_netbuf_list* list) {
  mtx_init(&list->lock, mtx_plain);
  list->priority = 0;
  list->qlen = 0;
  list_initialize(&list->listnode);
}

static inline bool brcmf_netbuf_maybe_in_list(struct brcmf_netbuf* netbuf) {
  return netbuf->listnode.next != NULL;
}

static inline uint32_t brcmf_netbuf_list_length(struct brcmf_netbuf_list* list) {
  uint32_t length;
  mtx_lock(&list->lock);
  length = list->qlen;
  mtx_unlock(&list->lock);
  return length;
}

static inline bool brcmf_netbuf_list_is_empty(struct brcmf_netbuf_list* list) {
  bool empty;
  mtx_lock(&list->lock);
  empty = list_is_empty(&list->listnode);
  ZX_DEBUG_ASSERT(empty == (list->qlen == 0));
  mtx_unlock(&list->lock);
  return empty;
}

static inline void brcmf_netbuf_list_add_head(struct brcmf_netbuf_list* list,
                                              struct brcmf_netbuf* element) {
  mtx_lock(&list->lock);
  list_add_head(&list->listnode, &element->listnode);
  list->qlen++;
  mtx_unlock(&list->lock);
}

static inline struct brcmf_netbuf* brcmf_netbuf_list_prev(struct brcmf_netbuf_list* list,
                                                          struct brcmf_netbuf* element) {
  struct brcmf_netbuf* netbuf;
  mtx_lock(&list->lock);
  netbuf = list_prev_type(&list->listnode, &element->listnode, struct brcmf_netbuf, listnode);
  mtx_unlock(&list->lock);
  return netbuf;
}

static inline struct brcmf_netbuf* brcmf_netbuf_list_next(struct brcmf_netbuf_list* list,
                                                          struct brcmf_netbuf* element) {
  struct brcmf_netbuf* netbuf;
  mtx_lock(&list->lock);
  netbuf = list_next_type(&list->listnode, &element->listnode, struct brcmf_netbuf, listnode);
  mtx_unlock(&list->lock);
  return netbuf;
}

static inline void brcmf_netbuf_list_add_tail(struct brcmf_netbuf_list* list,
                                              struct brcmf_netbuf* element) {
  mtx_lock(&list->lock);
  list_add_tail(&list->listnode, &element->listnode);
  list->qlen++;
  mtx_unlock(&list->lock);
}

static inline void brcmf_netbuf_list_add_after(struct brcmf_netbuf_list* list,
                                               struct brcmf_netbuf* target,
                                               struct brcmf_netbuf* new_element) {
  mtx_lock(&list->lock);
  list_add_after(&target->listnode, &new_element->listnode);
  list->qlen++;
  mtx_unlock(&list->lock);
}

static inline struct brcmf_netbuf* brcmf_netbuf_list_remove_head(struct brcmf_netbuf_list* list) {
  struct brcmf_netbuf* netbuf;
  mtx_lock(&list->lock);
  if (list->qlen == 0) {
    netbuf = NULL;
  } else {
    list->qlen--;
    netbuf = list_remove_head_type(&list->listnode, struct brcmf_netbuf, listnode);
  }
  mtx_unlock(&list->lock);
  return netbuf;
}

static inline void brcmf_netbuf_list_remove(struct brcmf_netbuf_list* list,
                                            struct brcmf_netbuf* element) {
  mtx_lock(&list->lock);
  ZX_DEBUG_ASSERT(list->qlen > 0);
  list->qlen--;
  list_delete(&element->listnode);
  mtx_unlock(&list->lock);
}

static inline struct brcmf_netbuf* brcmf_netbuf_list_remove_tail(struct brcmf_netbuf_list* list) {
  struct brcmf_netbuf* netbuf;
  mtx_lock(&list->lock);
  if (list->qlen == 0) {
    netbuf = NULL;
  } else {
    list->qlen--;
    netbuf = list_remove_tail_type(&list->listnode, struct brcmf_netbuf, listnode);
  }
  mtx_unlock(&list->lock);
  return netbuf;
}

static inline struct brcmf_netbuf* brcmf_netbuf_list_peek_head(struct brcmf_netbuf_list* list) {
  struct brcmf_netbuf* netbuf;
  mtx_lock(&list->lock);
  netbuf = list_peek_head_type(&list->listnode, struct brcmf_netbuf, listnode);
  mtx_unlock(&list->lock);
  return netbuf;
}

static inline struct brcmf_netbuf* brcmf_netbuf_list_peek_tail(struct brcmf_netbuf_list* list) {
  struct brcmf_netbuf* buf;
  mtx_lock(&list->lock);
  buf = list_peek_tail_type(&list->listnode, struct brcmf_netbuf, listnode);
  mtx_unlock(&list->lock);
  return buf;
}

#define brcmf_netbuf_list_for_every(buf_list, entry) \
  list_for_every_entry (&((buf_list)->listnode), entry, struct brcmf_netbuf, listnode)

#define brcmf_netbuf_list_for_every_safe(buf_list, entry, temp) \
  list_for_every_entry_safe (&((buf_list)->listnode), entry, temp, struct brcmf_netbuf, listnode)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_NETBUF_H_
