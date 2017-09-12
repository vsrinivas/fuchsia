// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

void lk_main(void) __NO_RETURN __EXTERNALLY_VISIBLE;
void lk_secondary_cpu_entry(void);
void lk_init_secondary_cpus(uint secondary_cpu_count);

__END_CDECLS
