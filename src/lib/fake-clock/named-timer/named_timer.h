// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FAKE_CLOCK_NAMED_TIMER_NAMED_TIMER_H_
#define SRC_LIB_FAKE_CLOCK_NAMED_TIMER_NAMED_TIMER_H_

#include <zircon/types.h>

__BEGIN_CDECLS

// Create a named deadline and return if successful.
// If successful, the absolute time of the deadline is recorded in |out|. This method is intended
// for use in conjunction with the fake-clock library. When fake-clock is linked in, the method is
// expected to succeed.
// Outside of an integration test, fake-clock is not available and this method will return false.
// In this case the caller should fall back to a standard method of calculating a deadline.
bool create_named_deadline(char* component, size_t component_len, char* code, size_t code_len,
                           zx_time_t duration, zx_time_t* out);

__END_CDECLS

#endif  // SRC_LIB_FAKE_CLOCK_NAMED_TIMER_NAMED_TIMER_H_
