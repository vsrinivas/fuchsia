// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fuchsia_device.h"

#include <lib/ddk/driver.h>

void iwl_device_release(struct device* device) { device_async_remove(device->zxdev); }
