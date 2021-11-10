// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_STREAM_IO_PAYLOAD_BUFFER_H_
#define SRC_MEDIA_VNEXT_LIB_STREAM_IO_PAYLOAD_BUFFER_H_

#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

namespace fmlib {

// Move-only object wrapping a payload buffer.
class PayloadBuffer {
 public:
  // Constructs an invalid |PayloadBuffer|.
  PayloadBuffer() = default;

  // Constructs a valid |PayloadBuffer| for a mapped buffer.
  PayloadBuffer(fuchsia::media2::PayloadRange payload_range, void* data)
      : is_valid_(true), payload_range_(payload_range), data_(data) {
    FX_CHECK(payload_range_.size != 0);
    FX_CHECK(data_);
  }

  // Constructs a valid |PayloadBuffer| for an unmapped buffer.
  explicit PayloadBuffer(fuchsia::media2::PayloadRange payload_range)
      : is_valid_(true), payload_range_(payload_range) {
    FX_CHECK(payload_range_.size != 0);
  }

  ~PayloadBuffer() { MaybeCompleteDestroyedCompleter(); }

  // Allow move.
  PayloadBuffer(PayloadBuffer&& other) noexcept {
    is_valid_ = other.is_valid_;
    destroyed_completer_ = std::move(other.destroyed_completer_);
    payload_range_ = other.payload_range_;
    data_ = other.data_;
    other.Reset();
  }
  PayloadBuffer& operator=(PayloadBuffer&& other) noexcept {
    // We're effectively destroying this buffer to copy over it, so we need to run the destroyed
    // completer, if it has been set.
    MaybeCompleteDestroyedCompleter();
    is_valid_ = other.is_valid_;
    destroyed_completer_ = std::move(other.destroyed_completer_);
    payload_range_ = other.payload_range_;
    data_ = other.data_;
    other.Reset();
    return *this;
  }

  // Disallow copy and assign.
  PayloadBuffer(const PayloadBuffer&) = delete;
  PayloadBuffer& operator=(const PayloadBuffer&) = delete;

  // Determines whether this |PayloadBUffer| is valid.
  bool is_valid() const { return is_valid_; }
  explicit operator bool() const { return is_valid_; }

  // Resets this |PayloadBUffer| to an invalid state.
  void Reset() {
    MaybeCompleteDestroyedCompleter();
    is_valid_ = false;
    payload_range_.buffer_id = 0;
    payload_range_.offset = 0;
    payload_range_.size = 0;
    data_ = nullptr;
  }

  // Returns the payload range for this payload buffer.
  const fuchsia::media2::PayloadRange& payload_range() const {
    FX_CHECK(is_valid());
    return payload_range_;
  }

  // Returns a pointer to the mapped payload area in process memory. If this payload buffer was
  // created unmapped, returns nullptr.
  void* data() const {
    FX_CHECK(is_valid());
    return data_;
  }

  // Returns the size of this payload buffer.
  size_t size() const {
    FX_CHECK(is_valid());
    return payload_range_.size;
  }

 private:
  // Returns a promise that completes when this buffer is destroyed or reset. This method may only
  // be called once for any given instance of |PayloadBuffer|. This is used by
  // |OutputBufferCollection| only.
  [[nodiscard]] fpromise::promise<> WhenDestroyed() {
    fpromise::bridge<> bridge;
    destroyed_completer_ = std::move(bridge.completer);
    return bridge.consumer.promise();
  }

  void MaybeCompleteDestroyedCompleter() {
    if (destroyed_completer_) {
      destroyed_completer_.complete_ok();
    }
  }

  bool is_valid_ = false;
  fpromise::completer<> destroyed_completer_;
  fuchsia::media2::PayloadRange payload_range_;
  void* data_ = nullptr;

  friend class OutputBufferCollection;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_STREAM_IO_PAYLOAD_BUFFER_H_
