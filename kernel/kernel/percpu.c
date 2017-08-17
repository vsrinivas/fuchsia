// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/percpu.h>

#include <arch/ops.h>

struct percpu percpu[SMP_MAX_CPUS];
