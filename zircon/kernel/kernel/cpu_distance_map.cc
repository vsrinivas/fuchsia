// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/cpu_distance_map.h"

lazy_init::LazyInit<CpuDistanceMap> g_cpu_distance_map;
