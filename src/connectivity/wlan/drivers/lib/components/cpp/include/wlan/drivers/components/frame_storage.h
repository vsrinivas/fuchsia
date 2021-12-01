// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_FRAME_STORAGE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_FRAME_STORAGE_H_

#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <string.h>
#include <zircon/compiler.h>

#include <mutex>
#include <optional>
#include <vector>

#include "frame.h"
#include "frame_container.h"

namespace wlan::drivers::components {

// A class for managing storage and lifetime of frames.
//
// In this context a frame is merely meta data representing a location in a VMO, a mapped address of
// that VMO location, the size of the data contained there and some additional data such as
// priority, port id and buffer id related to transmission and receiving of frames.
//
// The caller will populate the storage with frames and can then acquire single or multiple frames.
// The lifetime of these frames will be automatically managed such that when the user no longer
// needs a frame it will automatically be returned to the storage from which it came. Frames can
// also be returned manually if desired.
//
// Because the use cases for this class is almost certainly going to be multi threaded this class
// also acts as its own mutex. This allows references to this class to not also have to store a
// reference to a mutex stored somewhere else. Instead the caller will simply lock the storage for
// as long as needed and then unlock it. This allows efficient storage of references in Frame
// objects while also allowing thread annotation analysis to work properly. The locking and
// unlocking can be done manually if desired but we recommend the following pattern.
//
// FrameStorage storage;
// std::lock_guard lock(storage);
//
// This will maintain the lock on storage until the lock object goes out of scope. This will also
// work with other locking classes and mechanisms that operate on things that behave like an
// std::mutex.
class __TA_CAPABILITY("mutex") FrameStorage {
 public:
  using StorageType = std::vector<Frame>;
  using iterator = StorageType::iterator;
  using const_iterator = StorageType::const_iterator;

  FrameStorage() = default;
  ~FrameStorage() {
    // Manually clear storage here to prevent Frame destructors from returning frames to storage
    // as they are being destructed.
    clear();
  }
  FrameStorage(const FrameStorage&) = delete;
  FrameStorage& operator=(const FrameStorage&) = delete;

  void lock() __TA_ACQUIRE() { mutex_.lock(); }
  void unlock() __TA_RELEASE() { mutex_.unlock(); }

  void Store(const rx_space_buffer_t* buffers_list, size_t buffers_count, uint8_t* vmo_addrs[])
      __TA_REQUIRES(*this) {
    for (size_t i = 0; i < buffers_count; ++i) {
      const rx_space_buffer_t& buffer = buffers_list[i];
      const size_t offset = buffer.region.offset;
      uint8_t* const addr = vmo_addrs[buffer.region.vmo] + offset;
      const uint32_t length = static_cast<uint32_t>(buffer.region.length);
      storage_.emplace_back(this, buffer.region.vmo, offset, buffer.id, addr, length, 0);
    }
  }

  void Store(Frame&& frame) __TA_REQUIRES(*this) {
    ZX_DEBUG_ASSERT(frame.storage_ == this);
    storage_.emplace_back(std::move(frame));
  }

  void Store(FrameContainer&& frames) __TA_REQUIRES(*this) {
    storage_.reserve(storage_.size() + frames.size());
    for (auto& frame : frames) {
      storage_.emplace_back(std::move(frame));
    }
  }

  FrameContainer Acquire(size_t count) __TA_REQUIRES(*this) {
    if (count > storage_.size()) {
      return FrameContainer();
    }
    FrameContainer result;
    result.reserve(count);

    const size_t start = storage_.size() - count;
    const size_t end = start + count;
    for (size_t i = start; i < end; ++i) {
      result.emplace_back(std::move(storage_[i]));
    }
    storage_.resize(storage_.size() - count);

    return result;
  }

  std::optional<Frame> Acquire() __TA_REQUIRES(*this) {
    if (storage_.empty()) {
      return std::nullopt;
    }

    std::optional<Frame> frame = std::move(storage_.back());
    storage_.pop_back();

    return frame;
  }

  // Erase all frames with the given VMO id. This is useful when the underlying VMO is going away
  // and all frames backed by that VMO need to be removed.
  void EraseFramesWithVmoId(uint8_t vmo_id) {
    for (auto frame = storage_.begin(); frame != storage_.end();) {
      if (frame->VmoId() == vmo_id) {
        // This frame needs to be erased but first it must be released from storage so it's not
        // returned right away when it gets destructed.
        frame->ReleaseFromStorage();
        frame = storage_.erase(frame);
      } else {
        ++frame;
      }
    }
  }

  // Methods to make this behave like a standard container

  void clear() __TA_REQUIRES(*this) {
    // Frames need to be released from storage here, otherwise they will be inserted into to the
    // storage as they are being destructed.
    for (auto& frame : storage_) {
      frame.ReleaseFromStorage();
    }
    storage_.clear();
  }

  size_t size() const __TA_REQUIRES(*this) { return storage_.size(); }
  bool empty() const __TA_REQUIRES(*this) { return storage_.empty(); }

  const Frame& back() const __TA_REQUIRES(*this) { return storage_.back(); }
  const Frame& front() const __TA_REQUIRES(*this) { return storage_.front(); }

 private:
  StorageType storage_;
  std::mutex mutex_;
};

}  // namespace wlan::drivers::components

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_FRAME_STORAGE_H_
