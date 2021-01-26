// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_INTERCEPTORS_PACKET_LOSS_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_INTERCEPTORS_PACKET_LOSS_H_

#include <zircon/assert.h>

#include <random>

#include "interceptor.h"
#include "src/lib/fxl/macros.h"

namespace netemul {
namespace interceptor {

// PacketLoss emulation interceptor.
class PacketLoss : public Interceptor {
 public:
  // Creates a packet loss interceptor with the provided loss rate.
  //
  // |rng| must be a function that returns a random number in the range [0, 99].
  //
  // |loss_rate| is the rate of dropped packets, must be in the range [0, 100].
  PacketLoss(fit::function<uint8_t()> rng, uint8_t loss_rate, ForwardPacketCallback callback)
      : Interceptor(std::move(callback)), rng_(std::move(rng)), loss_rate_(loss_rate) {
    ZX_ASSERT(loss_rate <= 100);
  }

  PacketLoss(uint8_t loss_rate, ForwardPacketCallback callback)
      : PacketLoss(DefaultRNG(), loss_rate, std::move(callback)) {}

  void Intercept(InterceptPacket packet) override {
    uint8_t rnd = rng_();
    ZX_ASSERT(rnd < 100);
    // pass packet if random passes loss rate,
    // otherwise just lose the packet.
    if (rnd >= loss_rate_) {
      Forward(std::move(packet));
    }
  }

  std::vector<InterceptPacket> Flush() override { return std::vector<InterceptPacket>(); }

  static fit::function<uint8_t()> DefaultRNG() {
    std::uniform_int_distribution<uint8_t> dist(0, 99);
    return fit::function<uint8_t()>([dist]() mutable {
      std::random_device r;
      return dist(r);
    });
  }

 private:
  fit::function<uint8_t()> rng_;
  uint8_t loss_rate_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PacketLoss);
};

}  // namespace interceptor
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_INTERCEPTORS_PACKET_LOSS_H_
