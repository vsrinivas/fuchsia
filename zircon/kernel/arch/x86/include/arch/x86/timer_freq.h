// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

__BEGIN_CDECLS

// Returns the core crystal clock frequency if it can be found from the CPU
// alone (i.e. without calibration), or returns 0 if not.
uint64_t x86_lookup_core_crystal_freq(void);

// Returns the TSC frequency if it can be found from the CPU alone (i.e. without
// calibration), or returns 0 if not.
uint64_t x86_lookup_tsc_freq(void);

__END_CDECLS


