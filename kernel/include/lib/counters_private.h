// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// These are helper routines used to implement `k counters`, exposed
// here only for testing purposes. See the implementation for details.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

void counters_clean_up_values(const uint64_t* values_in, uint64_t* values_out, size_t* count_out);
uint64_t counters_get_percentile(const uint64_t* values, size_t count, uint64_t percentage_dot8);
bool counters_has_outlier(const uint64_t* values_in);

__END_CDECLS
