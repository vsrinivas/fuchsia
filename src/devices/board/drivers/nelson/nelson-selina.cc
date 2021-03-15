// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/platform-defs.h>
#include <zircon/status.h>

#include <ddk/binding.h>
#include <ddk/debug.h>

#include "nelson-gpios.h"
#include "nelson.h"

namespace nelson {

static constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};

static constexpr zx_bind_inst_t irq_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_SELINA_IRQ),
};

static constexpr zx_bind_inst_t reset_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_SELINA_RESET),
};

static constexpr zx_bind_inst_t spi_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SPI),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_INFINEON),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_INFINEON_BGT60TR13C),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_RADAR_SENSOR),
};

static constexpr device_fragment_part_t irq_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(irq_gpio_match), irq_gpio_match},
};

static constexpr device_fragment_part_t reset_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(reset_gpio_match), reset_gpio_match},
};

static constexpr device_fragment_part_t spi_fragment[] = {
    {countof(root_match), root_match},
    {countof(spi_match), spi_match},
};

static constexpr device_fragment_t fragments[] = {
    {"irq-gpio", countof(irq_gpio_fragment), irq_gpio_fragment},
    {"reset-gpio", countof(reset_gpio_fragment), reset_gpio_fragment},
    {"spi", countof(spi_fragment), spi_fragment},
};

static constexpr zx_device_prop_t props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_NELSON},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_RADAR_SENSOR},
};

static composite_device_desc_t composite_dev = []() {
  composite_device_desc_t desc = {};
  desc.props = props;
  desc.props_count = countof(props);
  desc.fragments = fragments;
  desc.fragments_count = countof(fragments);
  desc.coresident_device_index = UINT32_MAX;
  return desc;
}();

zx_status_t Nelson::SelinaInit() { return DdkAddComposite("selina", &composite_dev); }

}  // namespace nelson
