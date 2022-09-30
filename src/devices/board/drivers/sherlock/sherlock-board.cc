// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/gpio/c/banjo.h>
#include <lib/ddk/debug.h>

#include <soc/aml-t931/t931-gpio.h>

#include "sherlock.h"

namespace sherlock {
namespace fpbus = fuchsia_hardware_platform_bus;

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

  fpbus::BoardInfo info = {};
  info.board_revision() = id0 + (id1 << 1) + (id2 << 2) + (id3 << 3) + (id4 << 4);
  zxlogf(DEBUG, "%s: PBusSetBoardInfo to %X", __func__, *info.board_revision());
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('BOAR');
  auto result = pbus_.buffer(arena)->SetBoardInfo(fidl::ToWire(fidl_arena, info));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: SetBoard(info)Info Board(info) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: SetBoard(info)Info Board(info) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace sherlock
