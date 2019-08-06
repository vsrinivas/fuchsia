// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <zircon/compiler.h>

namespace {

struct EmptyStruct {};

struct Data {
  uint16_t foo;
  __NO_UNIQUE_ADDRESS EmptyStruct bar;
};

#if __has_cpp_attribute(no_unique_address)
static_assert(sizeof(Data) == sizeof(uint16_t));
#else
static_assert(sizeof(Data) > sizeof(uint16_t));
#endif

}  // namespace
