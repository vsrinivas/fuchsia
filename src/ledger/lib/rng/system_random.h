// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_RNG_SYSTEM_RANDOM_H_
#define SRC_LEDGER_LIB_RNG_SYSTEM_RANDOM_H_

#include <zircon/syscalls.h>

#include "src/ledger/lib/rng/random.h"

namespace ledger {

// Implementation of |Random| that uses the system RNG.
class SystemRandom final : public Random {
 private:
  void InternalDraw(void* buffer, size_t buffer_size) override {
    zx_cprng_draw(buffer, buffer_size);
  }
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_RNG_SYSTEM_RANDOM_H_
