// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <ddk/driver.h>

#include "test_support.h"

static zx_device_t* g_parent_device;

// This implementation shouldn't run any tests. It's used in the production
// driver.
zx_device_t* TestSupport::parent_device() { return g_parent_device; }

void TestSupport::set_parent_device(zx_device_t* handle) { g_parent_device = handle; }

bool TestSupport::RunAllTests() { return true; }
