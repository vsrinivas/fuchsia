// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

static mx_driver_ops_t intel_gen_gpu_driver_ops = {
};

// clang-format off
MAGENTA_DRIVER_BEGIN(_stub_gpu, intel_gen_gpu_driver_ops, "magma", "0.1", 5)
MAGENTA_DRIVER_END(_stub_gpu)
// clang-format on
