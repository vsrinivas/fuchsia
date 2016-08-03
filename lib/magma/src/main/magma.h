// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sys_driver/magma_driver.h"

#define MAGMA_START 1

#define MAGMA_UNIT_TESTS 0

#if MAGMA_UNIT_TESTS
#include "unit_tests/test_platform_device.h"
#include "gtest/gtest.h"
#endif

#define MAGMA_READBACK_TEST 0

#if MAGMA_READBACK_TEST
extern "C" {
#include <gpureadback.h>
}
#endif

static int magma_hook(void* dev);
