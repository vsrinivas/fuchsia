// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sys_driver/magma_driver.h"
#include "helper/platform_device_helper.h"
#include "gtest/gtest.h"

extern "C" {
#include <gpureadback.h>
}

#define MAGMA_START 1
#define MAGMA_UNIT_TESTS 0
#define MAGMA_READBACK_TEST 0

static int magma_hook(void* dev);
