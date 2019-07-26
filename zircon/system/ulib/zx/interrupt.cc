// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/interrupt.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t interrupt::create(const resource& resource, uint32_t vector, uint32_t options,
                              interrupt* result) {
  // Assume |result| uses a distinct container from |resource|, due to
  // strict aliasing.
  return zx_interrupt_create(resource.get(), vector, options, result->reset_and_get_address());
}

}  // namespace zx
