// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test.h"

#include <lib/ddk/platform-defs.h>

#include "test-resources.h"

namespace board_test {

zx_status_t TestBoard::TestInit() {
  pbus_dev_t test_dev = {};
  test_dev.name = "test-parent";
  test_dev.vid = PDEV_VID_TEST;
  test_dev.pid = PDEV_PID_PBUS_TEST;
  test_dev.did = PDEV_DID_TEST_PARENT;

  return pbus_.DeviceAdd(&test_dev);
}

}  // namespace board_test
