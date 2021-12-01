// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_FRAME_CONTAINER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_FRAME_CONTAINER_H_

#include <vector>

#include "frame.h"

namespace wlan::drivers::components {

// A class containing a collection of frames.
// This class behaves almost exactly like any standard container class. The main difference is in
// the lifetime management of the contained frames. On destruction the container will return all
// contained frames to storage if they are automatically managed. Note that this will acquire the
// lock on the storage so be careful to not already hold the lock in the same thread or there will
// be a deadlock.
class FrameContainer {
 public:
  using container = std::vector<Frame>;
  using iterator = container::iterator;
  using const_iterator = container::const_iterator;
  using value_type = container::value_type;

  FrameContainer() = default;
  ~FrameContainer() { ReturnToStorage(); }

  FrameContainer(const FrameContainer&) = delete;
  FrameContainer& operator=(const FrameContainer&) = delete;

  FrameContainer(FrameContainer&& other) = default;
  FrameContainer& operator=(FrameContainer&&) = default;

  iterator begin() { return container_.begin(); }
  iterator end() { return container_.end(); }

  const_iterator begin() const { return container_.begin(); }
  const_iterator end() const { return container_.end(); }

  Frame& operator[](size_t index) { return container_[index]; }
  const Frame& operator[](size_t index) const { return container_[index]; }

  bool empty() const { return container_.empty(); }
  size_t size() const { return container_.size(); }
  Frame* data() { return container_.data(); }
  const Frame* data() const { return container_.data(); }

  Frame& front() { return container_.front(); }
  const Frame& front() const { return container_.front(); }

  Frame& back() { return container_.back(); }
  const Frame& back() const { return container_.back(); }

  void clear() { container_.clear(); }
  void reserve(size_t size) { container_.reserve(size); }

  template <typename... Args>
  Frame& emplace_back(Args&&... args) {
    return container_.emplace_back(std::forward<Args>(args)...);
  }

 private:
  void ReturnToStorage();

  container container_;
};

}  // namespace wlan::drivers::components

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_FRAME_CONTAINER_H_
