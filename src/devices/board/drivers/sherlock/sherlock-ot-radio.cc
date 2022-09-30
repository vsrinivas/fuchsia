// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/ot-radio/ot-radio.h>
#include <limits.h>
#include <unistd.h>

#include <fbl/algorithm.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/sherlock-ot-radio-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace sherlock {
namespace fpbus = fuchsia_hardware_platform_bus;

static const uint32_t device_id = kOtDeviceNrf52840;
static const std::vector<fpbus::Metadata> nrf52840_radio_metadata{
    {{
        .type = DEVICE_METADATA_PRIVATE,
        .data =
            std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&device_id),
                                 reinterpret_cast<const uint8_t*>(&device_id) + sizeof(device_id)),
    }},
};

zx_status_t Sherlock::OtRadioInit() {
  fpbus::Node dev;
  dev.name() = "nrf52840-radio";
  dev.vid() = PDEV_VID_GENERIC;
  dev.pid() = PDEV_PID_SHERLOCK;
  dev.did() = PDEV_DID_OT_RADIO;
  dev.metadata() = nrf52840_radio_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('OTRA');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, nrf52840_radio_fragments,
                                               std::size(nrf52840_radio_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite OtRadio(dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite OtRadio(dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace sherlock
