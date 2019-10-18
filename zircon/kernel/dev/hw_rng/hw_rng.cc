// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/hw_rng.h>

const static struct hw_rng_ops* ops = nullptr;

size_t hw_rng_get_entropy(void* buf, size_t len) {
  if (ops != nullptr) {
    return ops->hw_rng_get_entropy(buf, len);
  } else {
    return 0;
  }
}

void hw_rng_register(const struct hw_rng_ops* new_ops) { ops = new_ops; }

bool hw_rng_is_registered() { return ops != nullptr; }
