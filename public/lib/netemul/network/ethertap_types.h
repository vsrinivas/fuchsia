// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETEMUL_NETWORK_ETHERTAP_TYPES_H_
#define LIB_NETEMUL_NETWORK_ETHERTAP_TYPES_H_

#include <limits.h>
#include <stdint.h>
#include <algorithm>
#include <random>

#define NETEMUL_MAC_LOCAL (0x02)
#define NETEMUL_MAC_MULTICAST (0x01)

namespace netemul {
class Mac {
 public:
  uint8_t d[6]{};

  bool IsLocallyAdministered() const {
    return static_cast<bool>(d[0] & NETEMUL_MAC_LOCAL);
  }

  bool IsMulticast() const {
    return static_cast<bool>(d[0] & NETEMUL_MAC_MULTICAST);
  }

  void SetLocallyAdministered() { d[0] |= NETEMUL_MAC_LOCAL; }

  void SetUnicast() { d[0] &= ~(NETEMUL_MAC_MULTICAST); }

  void SetMulticast() { d[0] |= NETEMUL_MAC_MULTICAST; }

  // helper to generate a random local unicast mac with a given string seed
  void RandomLocalUnicast(const std::string& str_seed) {
    std::seed_seq seed(str_seed.begin(), str_seed.end());
    std::independent_bits_engine<std::default_random_engine, CHAR_BIT, uint8_t>
        rnd(seed);
    std::generate(d, &d[6], rnd);
    SetUnicast();
    SetLocallyAdministered();
  }
};

}  // namespace netemul

#endif  // LIB_NETEMUL_NETWORK_ETHERTAP_TYPES_H_
