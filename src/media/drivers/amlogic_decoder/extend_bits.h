// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

uint64_t ExtendBits(uint64_t nearby_extended, uint64_t to_extend, uint32_t to_extend_low_order_bit_count);
