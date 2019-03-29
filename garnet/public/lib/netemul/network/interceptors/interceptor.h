// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETEMUL_NETWORK_INTERCEPTORS_INTERCEPTOR_H_
#define LIB_NETEMUL_NETWORK_INTERCEPTORS_INTERCEPTOR_H_

#include <lib/fit/function.h>
#include <src/lib/fxl/macros.h>
#include <vector>
#include "../consumer.h"

namespace netemul {

class InterceptPacket {
 public:
  InterceptPacket(const void* data, size_t len, data::Consumer::Ptr origin)
      : data_(len), origin_(std::move(origin)) {
    memcpy(data_.data(), data, len);
  }
  InterceptPacket(std::vector<uint8_t> data, data::Consumer::Ptr origin)
      : data_(std::move(data)), origin_(std::move(origin)) {}
  InterceptPacket(InterceptPacket&& other) = default;
  InterceptPacket& operator=(InterceptPacket&& other) = default;

  const std::vector<uint8_t>& data() { return data_; }
  const data::Consumer::Ptr& origin() { return origin_; }
  std::vector<uint8_t> TakeData() { return std::move(data_); }

 private:
  std::vector<uint8_t> data_;
  data::Consumer::Ptr origin_;
  FXL_DISALLOW_COPY_AND_ASSIGN(InterceptPacket);
};

// Abstract definition of base packet interceptors.
// Used for adverse network conditions emulation.
class Interceptor {
 public:
  using ForwardPacketCallback = fit::function<void(InterceptPacket)>;

  explicit Interceptor(ForwardPacketCallback forward)
      : forward_(std::move(forward)) {}

  virtual ~Interceptor() = default;
  // Intercepts a packet
  virtual void Intercept(InterceptPacket packet) = 0;
  // Flushes all packets that may be in interceptors internal memory.
  virtual std::vector<InterceptPacket> Flush() = 0;

 protected:
  void Forward(InterceptPacket packet) {
    if (forward_) {
      forward_(std::move(packet));
    }
  }

 private:
  ForwardPacketCallback forward_;
};

}  // namespace netemul

#endif  // LIB_NETEMUL_NETWORK_INTERCEPTORS_INTERCEPTOR_H_
