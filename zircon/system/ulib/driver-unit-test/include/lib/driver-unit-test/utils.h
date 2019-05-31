// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>

namespace driver_unit_test {

// Should be called by the driver run test hook before running the tests.
void SetParent(zx_device_t* parent);
// Retrieves the device set by |SetParent|.
zx_device_t* GetParent();

// Helper function for setting up and running driver unit tests. This should be called
// from the driver's run_unit_test hook.
//
// This sets the parent pointer and test logger.
// - |name| is an identifier for the test group.
// - |parent| is the parent device of the driver.
// - |channel| is where test logger output will be sent to.
bool RunZxTests(const char* name, zx_device_t* parent, zx_handle_t channel);

}  // namespace driver_unit_test
