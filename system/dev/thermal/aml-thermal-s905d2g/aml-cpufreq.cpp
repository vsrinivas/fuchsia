// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-cpufreq.h"
#include <ddk/debug.h>
#include <unistd.h>

namespace thermal {

namespace {

// CLK indexes.
constexpr uint32_t kSysPllDiv16 = 0;
constexpr uint32_t kSysCpuClkDiv16 = 1;

} // namespace

zx_status_t AmlCpuFrequency::Init(zx_device_t* parent) {

    // Get the clock protocol
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_CLK, &clk_protocol_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: failed to get clk protocol, status = %d", status);
        return status;
    }


    ddk::ClkProtocolProxy clk(&clk_protocol_);

    // Enable the following clocks so we can measure them
    // and calculate what the actual CPU freq is set to at
    // any given point.
    status = clk.Enable(kSysPllDiv16);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: failed to enable clock, status = %d\n", status);
        return status;
    }

    status = clk.Enable(kSysCpuClkDiv16);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: failed to enable clock, status = %d\n", status);
        return status;
    }
    return ZX_OK;
}

} // namespace thermal
