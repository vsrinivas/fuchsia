// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/syscalls.h>
#include <zircon/syscalls/smc.h>
#include <zircon/types.h>

namespace qcom {
    constexpr uint64_t kSmcInterrupted = 1;
    constexpr uint64_t kSmcOk = 0;
    constexpr uint64_t kSmcBusy = -13;

    zx_status_t SmcCall(zx_handle_t h, zx_smc_parameters_t* params, zx_smc_result_t* result);

} // namespace qcom
