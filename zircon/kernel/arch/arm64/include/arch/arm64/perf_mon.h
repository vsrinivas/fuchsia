// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <stdint.h>

#include <fbl/ref_ptr.h>
#include <vm/vm_object.h>

#include <arch/arm64.h>

#include <lib/zircon-internal/device/cpu-trace/arm64-pm.h>

// TODO(dje): Transition helpers.
using zx_arm64_pmu_properties_t = perfmon::Arm64PmuProperties;
using zx_arm64_pmu_config_t = perfmon::Arm64PmuConfig;

zx_status_t arch_perfmon_get_properties(zx_arm64_pmu_properties_t* state);

zx_status_t arch_perfmon_init();

zx_status_t arch_perfmon_assign_buffer(uint32_t cpu, fbl::RefPtr<VmObject> vmo);

zx_status_t arch_perfmon_stage_config(zx_arm64_pmu_config_t* config);

zx_status_t arch_perfmon_start();

zx_status_t arch_perfmon_stop();

zx_status_t arch_perfmon_fini();
