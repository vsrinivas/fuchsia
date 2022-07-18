// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_COMPAT_V1_TEST_H_
#define SRC_DEVICES_MISC_DRIVERS_COMPAT_V1_TEST_H_

#include <lib/ddk/driver.h>
#include <zircon/types.h>

#include <mutex>

struct V1Test {
  // The driver dispatcher is on a separate thread from the test thread,
  // so this is needed for proper synchronization.
  // TODO(fxbug.dev/103368): Fix test framework synchronization.
  std::mutex lock;

  zx_status_t status = ZX_OK;
  bool did_bind = false;
  bool did_create = false;
  bool did_release = false;
  zx_device_t* zxdev = nullptr;
};

#endif  // SRC_DEVICES_MISC_DRIVERS_COMPAT_V1_TEST_H_
