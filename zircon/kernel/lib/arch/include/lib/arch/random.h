// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_RANDOM_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_RANDOM_H_

#include <cstdint>
#include <optional>

namespace arch {

// There are two flavors of CPU random number generation:
//  1. reseeded occasionally as the hardware chooses
//  2. reseeded immediately on request
// The second offers stronger randomness guarantees when it's available,
// but may deplete the available random state more quickly than the first.
template <bool Reseed>
struct Random {
  // Returns true if the hardware supports the operation.
  // If this returns false, it's not safe to call Get()!
  [[gnu::const]] static bool Supported();

  // Fetch a random value if it can be gotten quickly.  If the hardware doesn't
  // succeed on the first try, This does a recommended standard number of
  // retries by default (argument is std::nullopt), or the exact number passed.
  // Returns std::nullopt if no value is immediately available.  Simply looping
  // will eventually make one available, but on a CPU not stressed with other
  // uses of the RNG hardware the standard retries should suffice so this
  // failing should be considered unusual.
  static std::optional<uint64_t> Get(std::optional<unsigned int> retries = std::nullopt);
};

extern template struct Random<false>;
extern template struct Random<true>;

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_RANDOM_H_
