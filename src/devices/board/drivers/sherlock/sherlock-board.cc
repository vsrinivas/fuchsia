// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gpio/c/banjo.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/debug.h>
#include <soc/aml-t931/t931-gpio.h>

#include "sherlock.h"

namespace sherlock {

zx_status_t Sherlock::BoardInit() {
  uint8_t id0, id1, id2, id3, id4;
  gpio_impl_.ConfigIn(T931_GPIO_HW_ID0, GPIO_NO_PULL);
  gpio_impl_.ConfigIn(T931_GPIO_HW_ID1, GPIO_NO_PULL);
  gpio_impl_.ConfigIn(T931_GPIO_HW_ID2, GPIO_NO_PULL);
  gpio_impl_.ConfigIn(T931_GPIO_HW_ID3, GPIO_NO_PULL);
  gpio_impl_.ConfigIn(T931_GPIO_HW_ID4, GPIO_NO_PULL);
  gpio_impl_.Read(T931_GPIO_HW_ID0, &id0);
  gpio_impl_.Read(T931_GPIO_HW_ID1, &id1);
  gpio_impl_.Read(T931_GPIO_HW_ID2, &id2);
  gpio_impl_.Read(T931_GPIO_HW_ID3, &id3);
  gpio_impl_.Read(T931_GPIO_HW_ID4, &id4);

  pbus_board_info_t info = {};
  info.board_revision = id0 + (id1 << 1) + (id2 << 2) + (id3 << 3) + (id4 << 4);
  zxlogf(DEBUG, "%s: PBusSetBoardInfo to %X", __func__, info.board_revision);
  zx_status_t status = pbus_.SetBoardInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PBusSetBoardInfo failed %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
