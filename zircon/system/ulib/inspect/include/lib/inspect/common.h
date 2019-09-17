// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_COMMON_H_
#define LIB_INSPECT_COMMON_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

const uint64_t kUniqueNameCounterId = 1;

// Increments the given inspect counter, returning its previous value.
//
// This function is thread safe.
uint64_t inspect_counter_increment(uint64_t counter_id);

// Resets the given inspect counter to 0.
//
// This function is thread safe.
void inspect_counter_reset(uint64_t counter_id);

__END_CDECLS

#endif  // LIB_INSPECT_COMMON_H_
