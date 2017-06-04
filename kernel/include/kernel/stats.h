// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <kernel/percpu.h>

#define CPU_STATS_INC(name) do { __atomic_fetch_add(&get_local_percpu()->stats.name, 1u, __ATOMIC_RELAXED); } while(0)

