// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/thermal/ntc.h>
#include <limits.h>

#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

namespace astro {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> saradc_mmios{
    {{
        .base = S905D2_SARADC_BASE,
        .length = S905D2_SARADC_LENGTH,
    }},
    {{
        .base = S905D2_AOBUS_BASE,
        .length = S905D2_AOBUS_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> saradc_irqs{
    {{
        .irq = S905D2_SARADC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

zx_status_t Astro::ThermistorInit() {
  thermal::NtcInfo ntc_info[] = {
      {.part = "ncpXXwf104",
       .profile =
           {
               {.temperature_c = -40, .resistance_ohm = 4397119},
               {.temperature_c = -35, .resistance_ohm = 3088599},
               {.temperature_c = -30, .resistance_ohm = 2197225},
               {.temperature_c = -25, .resistance_ohm = 1581881},
               {.temperature_c = -20, .resistance_ohm = 1151037},
               {.temperature_c = -15, .resistance_ohm = 846579},
               {.temperature_c = -10, .resistance_ohm = 628988},
               {.temperature_c = -5, .resistance_ohm = 471632},
               {.temperature_c = 0, .resistance_ohm = 357012},
               {.temperature_c = 5, .resistance_ohm = 272500},
               {.temperature_c = 10, .resistance_ohm = 209710},
               {.temperature_c = 15, .resistance_ohm = 162651},
               {.temperature_c = 20, .resistance_ohm = 127080},
               {.temperature_c = 25, .resistance_ohm = 100000},
               {.temperature_c = 30, .resistance_ohm = 79222},
               {.temperature_c = 35, .resistance_ohm = 63167},
               {.temperature_c = 40, .resistance_ohm = 50677},
               {.temperature_c = 45, .resistance_ohm = 40904},
               {.temperature_c = 50, .resistance_ohm = 33195},
               {.temperature_c = 55, .resistance_ohm = 27091},
               {.temperature_c = 60, .resistance_ohm = 22224},
               {.temperature_c = 65, .resistance_ohm = 18323},
               {.temperature_c = 70, .resistance_ohm = 15184},
               {.temperature_c = 75, .resistance_ohm = 12635},
               {.temperature_c = 80, .resistance_ohm = 10566},
               {.temperature_c = 85, .resistance_ohm = 8873},
               {.temperature_c = 90, .resistance_ohm = 7481},
               {.temperature_c = 95, .resistance_ohm = 6337},
               {.temperature_c = 100, .resistance_ohm = 5384},
               {.temperature_c = 105, .resistance_ohm = 4594},
               {.temperature_c = 110, .resistance_ohm = 3934},
               {.temperature_c = 115, .resistance_ohm = 3380},
               {.temperature_c = 120, .resistance_ohm = 2916},
               {.temperature_c = 125, .resistance_ohm = 2522},
           }},
  };

  thermal::NtcChannel ntc_channels[] = {
      {.adc_channel = 0, .pullup_ohms = 47000, .profile_idx = 0, .name = "therm-soc"},
      {.adc_channel = 1, .pullup_ohms = 47000, .profile_idx = 0, .name = "therm-wifi"},
      {.adc_channel = 2, .pullup_ohms = 47000, .profile_idx = 0, .name = "therm-dsp"},
      {.adc_channel = 3, .pullup_ohms = 47000, .profile_idx = 0, .name = "therm-ambient"},
  };

  std::vector<fpbus::Metadata> therm_metadata{
      {{
          .type = NTC_CHANNELS_METADATA_PRIVATE,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&ntc_channels),
              reinterpret_cast<const uint8_t*>(&ntc_channels) + sizeof(ntc_channels)),
      }},
      {{
          .type = NTC_PROFILE_METADATA_PRIVATE,
          .data =
              std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&ntc_info),
                                   reinterpret_cast<const uint8_t*>(&ntc_info) + sizeof(ntc_info)),
      }},
  };

  fpbus::Node thermistor;
  thermistor.name() = "thermistor";
  thermistor.vid() = PDEV_VID_GOOGLE;
  thermistor.pid() = PDEV_PID_ASTRO;
  thermistor.did() = PDEV_DID_AMLOGIC_THERMISTOR;
  thermistor.mmio() = saradc_mmios;
  thermistor.irq() = saradc_irqs;
  thermistor.metadata() = therm_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('THER');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, thermistor));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Thermistor(thermistor) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Thermistor(thermistor) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}
}  // namespace astro
