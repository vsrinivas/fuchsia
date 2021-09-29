// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_TESTS_INTEGRATION_TEST_MAGMA_H_
#define SRC_GRAPHICS_LIB_MAGMA_TESTS_INTEGRATION_TEST_MAGMA_H_

#include <stdbool.h>
#include <stdint.h>

extern uint32_t gVendorId;

bool test_magma_from_c(const char* device_name);

#endif  // SRC_GRAPHICS_LIB_MAGMA_TESTS_INTEGRATION_TEST_MAGMA_H_
