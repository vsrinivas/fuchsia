// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <test/reg32.h>

static_assert(TR32_FIELD2 == uint32_t{1} << 11);
static_assert(TR32_FIELD2_BIT == 11);
static_assert(TR32_RSVDZ == uint32_t{0x7e6});
static_assert(TR32_UNKNOWN == uint32_t{0x80000000});
static_assert(TR32_FIELD1_VALUE == uint32_t{1234});

int main() { return 0; }
