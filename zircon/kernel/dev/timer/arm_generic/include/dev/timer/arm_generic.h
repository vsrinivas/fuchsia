// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_TIMER_ARM_GENERIC_INCLUDE_DEV_TIMER_ARM_GENERIC_H_
#define ZIRCON_KERNEL_DEV_TIMER_ARM_GENERIC_INCLUDE_DEV_TIMER_ARM_GENERIC_H_

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

zx_time_t cntpct_to_zx_time(uint64_t cntpct);

__END_CDECLS

#endif  // ZIRCON_KERNEL_DEV_TIMER_ARM_GENERIC_INCLUDE_DEV_TIMER_ARM_GENERIC_H_
