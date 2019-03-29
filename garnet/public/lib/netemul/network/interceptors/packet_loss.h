// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETEMUL_NETWORK_INTERCEPTORS_PACKET_LOSS_H_
#define LIB_NETEMUL_NETWORK_INTERCEPTORS_PACKET_LOSS_H_

#include <src/lib/fxl/macros.h>
#include <zircon/assert.h>
#include <random>
#include "interceptor.h"

namespace netemul {
namespace interceptor {

inline uint8_t PacketLossDefaultRNG() {
  std::random_device dev;
  std::uniform_int_distribution<uint8_t> dist(0, 99);
  return dist(dev);
}

// PacketLoss emulation interceptor.
// RNG template parameter must be a function that returns a number
// between 0 and 99 (inclusive).
template <uint8_t RNG() = PacketLossDefaultRNG>
class PacketLoss : public Interceptor {
 public:
  PacketLoss(uint8_t loss_rate, ForwardPacketCallback callback)
      : Interceptor(std::move(callback)), loss_rate_(loss_rate) {
    ZX_ASSERT(loss_rate <= 100);
  }

  void Intercept(InterceptPacket packet) override {
    auto rnd = RNG();
    ZX_ASSERT(rnd < 100);
    // pass packet if random passes loss rate,
    // otherwise just lose the packet.
    if (rnd >= loss_rate_) {
      Forward(std::move(packet));
    }
  }

  std::vector<InterceptPacket> Flush() override {
    return std::vector<InterceptPacket>();
  }

 private:
  uint8_t loss_rate_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PacketLoss);
};

}  // namespace interceptor
}  // namespace netemul

#endif  // LIB_NETEMUL_NETWORK_INTERCEPTORS_PACKET_LOSS_H_
