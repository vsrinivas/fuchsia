// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <sys/types.h>

__BEGIN_CDECLS

zx_time_t cntpct_to_zx_time(uint64_t cntpct);

/* if freq_override != 0, use that as the operating frequency instead of CNTFRQ register */
void arm_generic_timer_init(int irq, uint32_t freq_override);

__END_CDECLS
