// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_FRAME_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_FRAME_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/assert.h>

#include <limits>

namespace wlan::drivers::components {

class Frame;
class FrameStorage;

// A class representing a data frame.
// This class contains meta data for a frame but not the actual data itself. It simply points to a
// VMO where the data is contained. The VMO is identified by an ID which can mean anything the user
// would like it to but it would typically match the VMO id from the network device's prepare VMO
// call. The frame object also contains an address to where the VMO has been mapped in memory as
// well as the size of the data residing in the VMO. Most commonly frames will all reside in the
// same VMO but have different offsets in the VMO and the frames are bounded by their size, creating
// individual slices of the VMO. Each individual frame will have a unique buffer ID meaning that one
// VMO (and therefore VMO ID) can contain multiple frames with distinct buffer IDs.
//
// Note that the buffer ID does not uniquely identify the location in the VMO and will not
// necessarily be the same for a particular location in the VMO every time. Therefore code should
// not rely on the buffer ID for any kind of global uniqueness or unique identifier within a VMO. It
// is merely a bookkeeping tool for the generic network device.
//
// The data pointer and size can be manipulated by shrinking and growing both head and tail as well
// as changing the size. The VMO offset will adjust accordingly which ensures that any operation
// that works on the VMO data can always use the VMO offset to get the correct data.
//
// Optionally each frame automatically manages its lifetime in relation to the storage it belongs
// to. This means that when an object of this class is destructed it will be returned to storage if
// it belongs to a storage instance. If a null pointer is provided as storage the frame will not be
// managed. For this management to be reliable a Frame can only be move constructed or move
// assigned. No copying is allowed. Frames can be manually returned to storage by calling
// ReturnToStorage or they can be released from storage by calling ReleaseFromStorage. Releasing a
// frame from storage will prevent it from automatically returning to storage on destruction.
//
// Note that returning a frame to storage, either automatically on destruction or manually, will
// acquire the lock on the storage. This means that the user has to be careful to not hold the lock
// on the storage in the same thread as frames are being returned or there will be a deadlock.
class Frame {
 public:
  Frame() = default;
  ~Frame() { ReturnToStorage(); }

  Frame(FrameStorage* storage, uint8_t vmo_id, size_t vmo_offset, uint16_t buffer_id, uint8_t* data,
        uint32_t size, uint8_t port_id)
      : storage_(storage),
        data_(data),
        size_(static_cast<uint16_t>(size)),
        original_size_(static_cast<uint16_t>(size)),
        headroom_(0),
        priority_(0),
        port_id_(port_id),
        vmo_offset_(static_cast<uint32_t>(vmo_offset)),
        buffer_id_(buffer_id),
        vmo_id_(vmo_id) {
    ZX_DEBUG_ASSERT(vmo_offset <= std::numeric_limits<uint32_t>::max());
    ZX_DEBUG_ASSERT(size <= std::numeric_limits<uint16_t>::max());
  }

  Frame(const Frame&) = delete;
  Frame& operator=(const Frame&) = delete;

  Frame(Frame&& other) noexcept
      : storage_(other.storage_),
        data_(other.data_),
        size_(other.size_),
        original_size_(other.original_size_),
        headroom_(other.headroom_),
        priority_(other.priority_),
        port_id_(other.port_id_),
        vmo_offset_(other.vmo_offset_),
        buffer_id_(other.buffer_id_),
        vmo_id_(other.vmo_id_) {
    // The other must no longer associate this frame with storage or we risk double returns.
    other.ReleaseFromStorage();
  }

  Frame& operator=(Frame&& other) noexcept {
    // If this instance owns a frame it must first be returned to storage.
    ReturnToStorage();

    storage_ = other.storage_;
    data_ = other.data_;
    size_ = other.size_;
    original_size_ = other.original_size_;
    headroom_ = other.headroom_;
    priority_ = other.priority_;
    vmo_offset_ = other.vmo_offset_;
    buffer_id_ = other.buffer_id_;
    vmo_id_ = other.vmo_id_;
    port_id_ = other.port_id_;

    // The other instance can no longer associate the frame with storage or we risk double returns.
    other.ReleaseFromStorage();

    return *this;
  }

  const uint8_t* Data() const { return data_; }
  uint8_t* Data() { return data_; }

  uint32_t Size() const { return size_; }
  void SetSize(uint32_t size) {
    ZX_DEBUG_ASSERT(size <= std::numeric_limits<uint16_t>::max());
    size_ = static_cast<uint16_t>(size);
  }

  uint16_t Headroom() const { return headroom_; }

  void ReturnToStorage();

  // Release ownership of a frame, after this it can no longer be returned to storage
  void ReleaseFromStorage() { storage_ = nullptr; }

  void ShrinkHead(uint32_t size) {
    ZX_DEBUG_ASSERT(size <= size_);
    headroom_ += static_cast<uint16_t>(size);
    data_ += size;
    size_ -= size;
  }

  void GrowHead(uint32_t size) {
    ZX_DEBUG_ASSERT(size <= headroom_);
    headroom_ -= static_cast<uint16_t>(size);
    data_ -= size;
    size_ += size;
  }

  void ShrinkTail(uint32_t size) {
    ZX_DEBUG_ASSERT(size <= size_);
    size_ -= size;
  }

  void GrowTail(uint32_t size) {
    ZX_DEBUG_ASSERT(size_ + size <= original_size_);
    size_ += size;
  }

  uint16_t BufferId() const { return buffer_id_; }

  uint8_t Priority() const { return priority_; }
  void SetPriority(uint8_t priority) { priority_ = priority; }

  size_t VmoOffset() const { return vmo_offset_ + headroom_; }
  uint8_t VmoId() const { return vmo_id_; }

  uint8_t PortId() const { return port_id_; }
  void SetPortId(uint8_t port_id) { port_id_ = port_id; }

 private:
  friend class FrameContainer;
  friend class FrameStorage;
  void Restore() {
    data_ -= headroom_;
    size_ = original_size_;
    headroom_ = 0;
  }

  FrameStorage* storage_ = nullptr;
  uint8_t* data_;
  uint16_t size_;
  uint16_t original_size_;

  uint16_t headroom_;
  uint8_t priority_;
  uint8_t port_id_;

  uint32_t vmo_offset_;
  uint16_t buffer_id_;
  uint8_t vmo_id_;
};

}  // namespace wlan::drivers::components

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_FRAME_H_
