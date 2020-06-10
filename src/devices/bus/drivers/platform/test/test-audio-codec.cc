// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include "test.h"

namespace board_test {

zx_status_t TestBoard::AudioCodecInit() {
  pbus_dev_t codec_dev = {};
  codec_dev.name = "codec";
  codec_dev.vid = PDEV_VID_TEST;
  codec_dev.pid = PDEV_PID_PBUS_TEST;
  codec_dev.did = PDEV_DID_TEST_AUDIO_CODEC;

  zx_status_t status = pbus_.DeviceAdd(&codec_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_test
