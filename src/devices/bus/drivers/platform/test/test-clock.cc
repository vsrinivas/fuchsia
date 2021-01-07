// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/clock.h>
#include <ddk/platform-defs.h>

#include "test.h"

namespace board_test {

namespace {
static const clock_id_t clock_ids[] = {
    {1},
    {3},
    {5},
    {7},
};

static const pbus_metadata_t clock_metadata[] = {
    {
        .type = DEVICE_METADATA_CLOCK_IDS,
        .data_buffer = &clock_ids,
        .data_size = sizeof(clock_ids),
    },
};
}  // namespace

zx_status_t TestBoard::ClockInit() {
  pbus_dev_t clock_dev = {};
  clock_dev.name = "clock";
  clock_dev.vid = PDEV_VID_TEST;
  clock_dev.pid = PDEV_PID_PBUS_TEST;
  clock_dev.did = PDEV_DID_TEST_CLOCK;
  clock_dev.metadata_list = clock_metadata;
  clock_dev.metadata_count = countof(clock_metadata);

  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_CLOCK_IMPL, &clock_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_test
