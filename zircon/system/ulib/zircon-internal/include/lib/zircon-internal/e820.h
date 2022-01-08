// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZIRCON_INTERNAL_E820_H_
#define LIB_ZIRCON_INTERNAL_E820_H_

#include <stdint.h>
#include <zircon/compiler.h>

enum class E820Type : uint32_t {
  kRam = 1,
  kReserved = 2,
  kAcpi = 3,
  kNvs = 4,
  kUnusable = 5,
};

struct E820Entry {
  uint64_t addr;
  uint64_t size;
  E820Type type;
} __PACKED;

#endif  // LIB_ZIRCON_INTERNAL_E820_H_
